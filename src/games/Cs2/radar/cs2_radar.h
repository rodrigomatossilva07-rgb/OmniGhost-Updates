#pragma once

#include <string>

namespace CS2 { struct Runtime; }

namespace CS2::WebRadar {


bool Start(int port);
void Stop();
bool IsRunning() noexcept;
int Port() noexcept;



void SubmitSnapshot(const Runtime& snapshot);


std::string LocalUrl();
std::string LocalIp();

std::string PublicUrl();
std::string AccessToken();

bool StartCloudflare();
void StopCloudflare();
bool CloudflareRunning() noexcept;
std::string CloudflareStatus();


std::string BuildLiveJson(const Runtime& runtime);
std::string BuildLiveJson();

}
