#include "fivem_web_radar.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <bcrypt.h>
#include <shellapi.h>

#include "../game/esp_manager.h"
#include "../game/offsets.h"
#include "../esp/esp.h"
#include "../esp/vehicle_esp.h"
#include "platform/embedded_resources.h"
#include "platform/radar_access.h"
#include "platform/runtime_bootstrap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace FiveM::WebRadar {
namespace {
using Clock = std::chrono::steady_clock;
constexpr char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kMaxRequest = 16 * 1024;
constexpr std::size_t kMaxClients = 16;
constexpr auto kPushPeriod = std::chrono::milliseconds(100);
constexpr auto kStaleAfter = std::chrono::milliseconds(1500);

struct Client {
    SOCKET socket = INVALID_SOCKET;
    bool websocket = false;
    Clock::time_point lastSeen{};
    std::string pending;
    std::size_t sent = 0;
};
std::atomic<bool> g_running{false}, g_stop{false};
std::atomic<std::uint32_t> g_clientCount{0};
std::atomic<bool> g_sourceConnected{false};
std::thread g_thread;
SOCKET g_listener = INVALID_SOCKET;
int g_port = 8765;
std::mutex g_stateMutex;
std::string g_token, g_json = R"({"type":"snapshot","connected":false,"players":[],"vehicles":[]})";
Clock::time_point g_sampleTime{};
std::uint32_t g_players = 0, g_vehicles = 0;
std::uint64_t g_sessionSalt = 0;

std::mutex g_cloudMutex;
PROCESS_INFORMATION g_cloudProcess{};
std::thread g_cloudThread;
std::atomic<bool> g_cloudStop{false};
std::atomic<bool> g_cloudWorkerActive{false};
std::string g_publicUrl, g_cloudStatus;

std::string Escape(std::string_view in) {
    std::string out; out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) { case '\\': out += "\\\\"; break; case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
        default: if (c >= 0x20) out += static_cast<char>(c); }
    }
    return out;
}

std::uint64_t Mix(std::uint64_t x) noexcept {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL; return x ^ (x >> 33);
}
std::string StableId(std::uintptr_t ped, std::uint32_t net) {
    if (net) return "net-" + std::to_string(net);
    std::ostringstream o; o << "session-" << std::hex << Mix(static_cast<std::uint64_t>(ped) ^ g_sessionSalt);
    return o.str();
}
bool Finite(const Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

std::string BuildFromCaches(std::uint32_t& playerCount, std::uint32_t& vehicleCount) {
    const auto snap = FiveM::ESP::GetValidPedsSnapshot();
    const auto local = FiveM::ESP::GetFrameLocalPos();
    const auto now = Clock::now();
    if (snap.timestamp.time_since_epoch().count()==0 || now-snap.timestamp > kStaleAfter || !Finite(local)) {
        playerCount=vehicleCount=0;
        return R"({"type":"snapshot","version":1,"connected":false,"players":[],"vehicles":[]})";
    }
    std::ostringstream o; o << std::fixed << std::setprecision(2);
    o << "{\"type\":\"snapshot\",\"version\":1,\"connected\":true,\"sampledAt\":"
      << GetTickCount64() << ",\"players\":[";
    bool first=true; playerCount=0;
    const std::size_t count=(std::min)(snap.validPeds.size(), snap.positions.size());
    for(std::size_t i=0;i<count && playerCount<256;++i) {
        const auto ped=snap.validPeds[i]; Vec3 p=snap.positions[i];
        if(!ped || !Finite(p) || p.IsZero()) continue;
        std::uint32_t net=0; esp::try_get_prepared_network_id(ped,net);
        char name[48]{}; esp::try_get_prepared_player_name(ped,name,sizeof(name));
        float health=0, armor=0; const bool hasHealth=esp::try_get_prepared_health(ped,health);
        const bool hasArmor=esp::try_get_prepared_armor(ped,armor);
        bool visible=false, visibilityKnown=false; esp::try_get_prepared_visibility(ped,visible,visibilityKnown);
        Vec3 velocity{}; const bool moving=FiveM::ESP::try_get_prepared_velocity(ped,velocity) &&
            (velocity.x*velocity.x+velocity.y*velocity.y)>.01f;
        if(!first)o<<','; first=false;
        o << "{\"id\":\""<<StableId(ped,net)<<"\",\"name\":\""<<Escape(name[0]?name:"Jogador")
          <<"\",\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"z\":"<<p.z
          <<",\"distance\":"<<p.distance_to(local)<<",\"local\":"<<(ped==FiveM::offset::localplayer?"true":"false");
        if(net)o<<",\"networkId\":"<<net;
        if(hasHealth)o<<",\"health\":"<<health;
        if(hasArmor)o<<",\"armor\":"<<armor;
        if(visibilityKnown)o<<",\"visible\":"<<(visible?"true":"false");
        if(moving)o<<",\"heading\":"<<std::atan2(velocity.x,velocity.y)*57.2957795;
        o << '}'; ++playerCount;
    }
    o << "],\"vehicles\":["; first=true; vehicleCount=0;
    for(const auto& v: vehicle_esp::GetSnapshot()) {
        if(!v.valid || !Finite(v.position) || vehicleCount>=128)continue;
        if(!first)o<<','; first=false;
        o<<"{\"id\":\"vehicle-"<<std::hex<<Mix(v.address^g_sessionSalt)<<std::dec
         <<"\",\"x\":"<<v.position.x<<",\"y\":"<<v.position.y<<",\"z\":"<<v.position.z
         <<",\"distance\":"<<v.distance<<",\"occupied\":"<<(v.occupied?"true":"false")<<"}";
        ++vehicleCount;
    }
    o << "]}"; return o.str();
}

std::string Base64(const unsigned char* data, std::size_t n) {
    static constexpr char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((n+2)/3*4);
    for(std::size_t i=0;i<n;i+=3){unsigned v=data[i]<<16; if(i+1<n)v|=data[i+1]<<8;if(i+2<n)v|=data[i+2];
      out+=t[(v>>18)&63];out+=t[(v>>12)&63];out+=(i+1<n?t[(v>>6)&63]:'=');out+=(i+2<n?t[v&63]:'=');}
    return out;
}
std::string WebSocketAccept(const std::string& key) {
    const std::string input=key+kWsGuid; std::array<unsigned char,20> digest{};
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; DWORD objSize=0,got=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA1_ALGORITHM,nullptr,0)<0)return{};
    BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objSize),sizeof(objSize),&got,0);
    std::vector<unsigned char> obj(objSize);
    bool ok=BCryptCreateHash(alg,&hash,obj.data(),objSize,nullptr,0,0)>=0 &&
      BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),static_cast<ULONG>(input.size()),0)>=0 &&
      BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
    return ok?Base64(digest.data(),digest.size()):std::string{};
}
std::string Header(const std::string& request,std::string_view name) {
    const std::string needle=std::string(name)+":"; std::size_t p=0;
    while((p=request.find('\n',p))!=std::string::npos){++p;if(_strnicmp(request.c_str()+p,needle.c_str(),needle.size())==0){
      p+=needle.size();while(p<request.size()&&(request[p]==' '||request[p]=='\t'))++p; auto e=request.find('\r',p);return request.substr(p,e-p);}}
    return{};
}
bool SendAll(SOCKET s,const char* data,std::size_t n){while(n){int sent=send(s,data,static_cast<int>((std::min)(n,std::size_t(INT_MAX))),0);if(sent<=0)return false;data+=sent;n-=sent;}return true;}
std::string WsFrame(std::string_view payload,unsigned char opcode=1){std::string f;f.push_back(static_cast<char>(0x80|opcode));
  if(payload.size()<126)f.push_back(static_cast<char>(payload.size()));else if(payload.size()<=0xffff){f.push_back(126);f.push_back(static_cast<char>((payload.size()>>8)&255));f.push_back(static_cast<char>(payload.size()&255));}else{f.push_back(127);for(int shift=56;shift>=0;shift-=8)f.push_back(static_cast<char>((static_cast<std::uint64_t>(payload.size())>>shift)&255));}
  f.append(payload);return f;}
void QueueWs(Client& c,std::string_view payload,unsigned char opcode=1){if(c.pending.empty()){c.pending=WsFrame(payload,opcode);c.sent=0;}else if(c.sent==0&&opcode==1){c.pending=WsFrame(payload,opcode);}}
bool FlushWs(Client& c){while(c.sent<c.pending.size()){const int n=send(c.socket,c.pending.data()+c.sent,static_cast<int>((std::min)(c.pending.size()-c.sent,std::size_t(INT_MAX))),0);if(n>0){c.sent+=static_cast<std::size_t>(n);continue;}if(n==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK)return true;return false;}c.pending.clear();c.sent=0;return true;}
std::string Http(int code,std::string_view status,std::string_view type,std::string_view body){std::ostringstream o;o<<"HTTP/1.1 "<<code<<' '<<status<<"\r\nContent-Type: "<<type<<"\r\nContent-Length: "<<body.size()<<"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n"<<body;return o.str();}
std::string Asset(std::string_view logical){auto bytes=OmniGhost::LoadEmbeddedResource(logical);return bytes?std::string(reinterpret_cast<const char*>(bytes->data()),bytes->size()):std::string{};}

void Close(Client& c){if(c.socket!=INVALID_SOCKET){shutdown(c.socket,SD_BOTH);closesocket(c.socket);c.socket=INVALID_SOCKET;}}
void HandleHttp(SOCKET s,const std::string& req,std::vector<Client>& clients){
    const auto lineEnd=req.find("\r\n"); if(lineEnd==std::string::npos)return; const auto first=req.substr(0,lineEnd);
    const auto a=first.find(' '),b=first.find(' ',a+1); if(a==std::string::npos||b==std::string::npos)return;
    std::string path=first.substr(a+1,b-a-1); const auto q=path.find('?');if(q!=std::string::npos)path.resize(q);
    if(path=="/healthz"){auto r=Http(200,"OK","application/json","{\"ok\":true}");SendAll(s,r.data(),r.size());return;}
    if(path=="/ws"){
      const std::string protocol=Header(req,"Sec-WebSocket-Protocol");
      const std::string accepted = "omnighost-radar." + g_token;
      if(!OmniGhost::RadarAccess::HasSafeSameOrigin(req) ||
         !OmniGhost::RadarAccess::HasValidWebSocketProtocolToken(req,g_token)){auto r=Http(401,"Unauthorized","text/plain","Unauthorized");SendAll(s,r.data(),r.size());return;}
      const auto key=Header(req,"Sec-WebSocket-Key"),accept=WebSocketAccept(key);if(accept.empty())return;
      std::ostringstream h;h<<"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "<<accept<<"\r\nSec-WebSocket-Protocol: "<<accepted<<"\r\n\r\n";
      const auto hs=h.str();if(!SendAll(s,hs.data(),hs.size()))return;u_long nb=1;ioctlsocket(s,FIONBIO,&nb);clients.push_back({s,true,Clock::now()});return;
    }
    std::string body,type;
    if(path=="/"||path=="/index.html"){body=Asset("fivem/webradar/index.html");type="text/html; charset=utf-8";}
    else if(path=="/app.js"){body=Asset("fivem/webradar/app.js");type="application/javascript; charset=utf-8";}
    else if(path=="/styles.css"){body=Asset("fivem/webradar/styles.css");type="text/css; charset=utf-8";}
    else if(path=="/manifest.json"){body=Asset("fivem/webradar/manifest.json");type="application/manifest+json; charset=utf-8";}
    else if(path=="/sw.js"){body=Asset("fivem/webradar/sw.js");type="application/javascript; charset=utf-8";}
    else if(path=="/omnighost.png"){body=Asset("fivem/webradar/omnighost.png");type="image/png";}
    if(body.empty()){auto r=Http(404,"Not Found","text/plain","Not found");SendAll(s,r.data(),r.size());return;}
    auto r=Http(200,"OK",type,body);SendAll(s,r.data(),r.size());
}

void ServerLoop(){WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w)!=0){g_running=false;return;}g_listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(static_cast<u_short>(g_port));inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
  int reuse=1;setsockopt(g_listener,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<char*>(&reuse),sizeof(reuse));
  if(g_listener==INVALID_SOCKET||bind(g_listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))||listen(g_listener,8)){if(g_listener!=INVALID_SOCKET)closesocket(g_listener);g_listener=INVALID_SOCKET;WSACleanup();g_running=false;return;}
  u_long nb=1;ioctlsocket(g_listener,FIONBIO,&nb);g_running=true;std::vector<Client> clients;auto lastPush=Clock::now()-kPushPeriod;auto lastPing=Clock::now();
  while(!g_stop){
    SOCKET s=accept(g_listener,nullptr,nullptr);if(s!=INVALID_SOCKET){if(clients.size()<kMaxClients){std::string req;char buf[4096];for(int loops=0;loops<5&&req.size()<kMaxRequest;++loops){int n=recv(s,buf,sizeof(buf),0);if(n<=0)break;req.append(buf,n);if(req.find("\r\n\r\n")!=std::string::npos)break;}HandleHttp(s,req,clients);if(std::none_of(clients.begin(),clients.end(),[&](const Client&c){return c.socket==s;}))closesocket(s);}else closesocket(s);}
    const auto now=Clock::now();if(now-lastPush>=kPushPeriod){PublishFromCaches();std::string json;{std::scoped_lock l(g_stateMutex);json=g_json;}
      for(auto&c:clients)if(c.socket!=INVALID_SOCKET)QueueWs(c,json);lastPush=now;}
    if(now-lastPing>=std::chrono::seconds(10)){for(auto&c:clients)if(c.socket!=INVALID_SOCKET&&c.pending.empty())QueueWs(c,"",9);lastPing=now;}
    for(auto&c:clients)if(c.socket!=INVALID_SOCKET&&!FlushWs(c))Close(c);
    for(auto&c:clients)if(c.socket!=INVALID_SOCKET){char b[256];int n=recv(c.socket,b,sizeof(b),0);if(n==0)Close(c);else if(n>0){c.lastSeen=now;const auto op=static_cast<unsigned char>(b[0])&0x0f;if(op==8)Close(c);}if(c.socket!=INVALID_SOCKET&&now-c.lastSeen>std::chrono::seconds(30))Close(c);}
    clients.erase(std::remove_if(clients.begin(),clients.end(),[](const Client&c){return c.socket==INVALID_SOCKET;}),clients.end());g_clientCount=static_cast<std::uint32_t>(clients.size());Sleep(5);
  }
  for(auto&c:clients)Close(c);closesocket(g_listener);g_listener=INVALID_SOCKET;WSACleanup();g_clientCount=0;g_running=false;
}

std::wstring CloudflaredPath(){std::wstring err;if(OmniGhost::RuntimeBootstrap::MaterializePrivateRuntimeFile(L"libs/cloudflared.exe",err))return OmniGhost::RuntimeBootstrap::PrivateRuntimePath(L"libs/cloudflared.exe").wstring();return L"libs\\cloudflared.exe";}
void CloudLoop(){SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rd=nullptr,wr=nullptr;if(!CreatePipe(&rd,&wr,&sa,0)){std::scoped_lock l(g_cloudMutex);g_cloudStatus="Falha ao criar canal do túnel.";g_cloudWorkerActive=false;return;}SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
  STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=wr;si.hStdError=wr;
  const auto exe=CloudflaredPath();std::wstring cmd=L"\""+exe+L"\" tunnel --no-autoupdate --protocol http2 --url http://127.0.0.1:"+std::to_wstring(g_port);std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());mutableCmd.push_back(0);
  PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),mutableCmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){CloseHandle(rd);CloseHandle(wr);std::scoped_lock l(g_cloudMutex);g_cloudStatus="Não foi possível iniciar cloudflared.";g_cloudWorkerActive=false;return;}CloseHandle(wr);{std::scoped_lock l(g_cloudMutex);g_cloudProcess=pi;g_cloudStatus="A criar link público...";}
  std::string text,pendingUrl;char b[1024];while(!g_cloudStop){DWORD avail=0;if(PeekNamedPipe(rd,nullptr,0,nullptr,&avail,nullptr)&&avail){DWORD got=0;if(ReadFile(rd,b,(std::min)(avail,DWORD(sizeof(b))),&got,nullptr)&&got){text.append(b,got);if(text.size()>32768)text.erase(0,text.size()-16384);auto p=text.find("https://");while(p!=std::string::npos){auto e=text.find_first_of(" \r\n\t",p);auto u=text.substr(p,e-p);if(u.find(".trycloudflare.com")!=std::string::npos){pendingUrl=u;break;}p=text.find("https://",p+8);}if(!pendingUrl.empty()&&text.find("Registered tunnel connection")!=std::string::npos){std::scoped_lock l(g_cloudMutex);g_publicUrl=pendingUrl+"/#"+g_token;g_cloudStatus="Túnel ligado e pronto.";}}}
    if(WaitForSingleObject(pi.hProcess,50)==WAIT_OBJECT_0)break;}
  if(g_cloudStop)TerminateProcess(pi.hProcess,0);WaitForSingleObject(pi.hProcess,3000);CloseHandle(rd);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);{std::scoped_lock l(g_cloudMutex);g_cloudProcess={};if(!g_cloudStop){g_publicUrl.clear();g_cloudStatus="Ligação Cloudflare interrompida; a tentar novamente...";}}g_cloudWorkerActive=false;
}
}

bool Start(int port){if(g_running)return true;if(g_thread.joinable())g_thread.join();g_port=std::clamp(port,1024,65535);std::string tokenError;if(!OmniGhost::RadarAccess::GenerateSessionToken(g_token,tokenError))return false;std::random_device rd;g_sessionSalt=(static_cast<std::uint64_t>(rd())<<32)^rd();g_stop=false;g_thread=std::thread(ServerLoop);for(int i=0;i<100&&!g_running;++i)Sleep(10);return g_running;}
void Stop(){StopCloudflare();g_stop=true;if(g_listener!=INVALID_SOCKET)shutdown(g_listener,SD_BOTH);if(g_thread.joinable())g_thread.join();g_sourceConnected=false;std::scoped_lock l(g_stateMutex);g_json=R"({"type":"snapshot","connected":false,"players":[],"vehicles":[]})";g_sampleTime={};g_players=g_vehicles=0;}
bool IsRunning() noexcept{return g_running;} int Port() noexcept{return g_port;}
void PublishFromCaches(){std::uint32_t pc=0,vc=0;std::string json=BuildFromCaches(pc,vc);g_sourceConnected=json.find("\"connected\":true")!=std::string::npos;std::scoped_lock l(g_stateMutex);g_json=std::move(json);g_sampleTime=Clock::now();g_players=pc;g_vehicles=vc;}
std::string AccessToken(){return g_token;} std::string LocalUrl(){return "http://127.0.0.1:"+std::to_string(g_port)+"/#"+g_token;} std::string PublicUrl(){std::scoped_lock l(g_cloudMutex);return g_publicUrl;}
bool StartCloudflare(){if(!g_running&&!Start(g_port))return false;if(g_cloudThread.joinable()){if(g_cloudWorkerActive||CloudflareRunning())return true;g_cloudThread.join();}g_cloudStop=false;g_cloudWorkerActive=true;{std::scoped_lock l(g_cloudMutex);g_publicUrl.clear();g_cloudStatus="A iniciar túnel...";}try{g_cloudThread=std::thread(CloudLoop);}catch(...){g_cloudWorkerActive=false;std::scoped_lock l(g_cloudMutex);g_cloudStatus="Não foi possível criar a thread do túnel.";return false;}return true;}
void StopCloudflare(){g_cloudStop=true;{std::scoped_lock l(g_cloudMutex);if(g_cloudProcess.hProcess)TerminateProcess(g_cloudProcess.hProcess,0);}if(g_cloudThread.joinable())g_cloudThread.join();std::scoped_lock l(g_cloudMutex);g_publicUrl.clear();g_cloudStatus.clear();}
bool CloudflareRunning() noexcept{std::scoped_lock l(g_cloudMutex);return g_cloudProcess.hProcess!=nullptr;}std::string CloudflareStatus(){std::scoped_lock l(g_cloudMutex);return g_cloudStatus;}
Diagnostics GetDiagnostics() noexcept{Diagnostics d;d.running=g_running;d.clients=g_clientCount;std::scoped_lock l(g_stateMutex);d.players=g_players;d.vehicles=g_vehicles;d.sourceFresh=g_sourceConnected&&g_sampleTime.time_since_epoch().count()!=0&&Clock::now()-g_sampleTime<kStaleAfter;d.lastSampleAgeMs=g_sampleTime.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-g_sampleTime).count():0;return d;}
}
