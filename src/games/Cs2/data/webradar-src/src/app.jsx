import {useEffect,useMemo,useRef,useState} from 'react';

const DEFAULTS={names:true,health:true,weapons:true,directions:true,bomb:true,panels:true,elevation:true,healthRings:true,iconSize:18,textSize:12,zoom:1,opacity:96,smoothing:76,ct:'#55b7ff',t:'#f3bd36'};
const MAPS=new Set(['de_mirage','de_inferno','de_dust2','de_nuke','de_ancient','de_anubis','de_vertigo','de_overpass','de_train']);
const LABELS={connected:'Ligado',connecting:'A ligar',reconnecting:'A religar',disconnected:'Desligado',error:'Erro'};
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const validNumber=v=>Number.isFinite(Number(v));
const loadSettings=()=>{try{return {...DEFAULTS,...JSON.parse(localStorage.getItem('omnighost.cs2.radar')||'{}')}}catch{return DEFAULTS}};
const radarPos=(meta,p)=>({x:(p.x-meta.x)/(meta.scale*1024)*100,y:-((p.y-meta.y)/(meta.scale*1024))*100});
const token=location.hash.slice(1);
const cleanWeapon=value=>(value||'').replace(/^weapon_/,'').replaceAll('_',' ').toUpperCase();

function useRadarSocket(){
  const [state,setState]=useState({status:'connecting',snapshot:null,latency:0,error:''});
  useEffect(()=>{let ws,timer,stale,stopped=false,attempt=0;
    const clear=()=>{clearTimeout(timer);clearTimeout(stale)};
    const retry=error=>{if(stopped)return;clear();setState(s=>({...s,status:'reconnecting',snapshot:null,error}));timer=setTimeout(connect,Math.min(8000,750*2**Math.min(attempt++,4)))};
    const connect=()=>{if(stopped)return;setState(s=>({...s,status:attempt?'reconnecting':'connecting',error:''}));
      const proto=location.protocol==='https:'?'wss:':'ws:';
      try{ws=new WebSocket(`${proto}//${location.host}/ws`,[`omnighost-radar.${token}`])}catch(e){retry(String(e));return}
      const opened=performance.now();
      ws.onopen=()=>{attempt=0;setState(s=>({...s,status:'connected',latency:Math.round(performance.now()-opened)}))};
      ws.onmessage=e=>{try{const d=JSON.parse(e.data);if(d.type!=='snapshot'||d.version!==1||!Array.isArray(d.players))return;
        clearTimeout(stale);stale=setTimeout(()=>setState(s=>({...s,status:'disconnected',snapshot:null,error:'Dados desatualizados'})),1800);
        setState(s=>({...s,status:d.connected?'connected':'disconnected',snapshot:d.connected?d:null,error:d.connected?'':'Sem dados recentes'}));
      }catch{setState(s=>({...s,error:'Mensagem inválida ignorada'}))}};
      ws.onerror=()=>{};ws.onclose=()=>retry('Ligação interrompida');
    };
    if(!token)setState({status:'error',snapshot:null,latency:0,error:'Token de acesso ausente'});else connect();
    return()=>{stopped=true;clear();if(ws)ws.close()};
  },[]);return state;
}

function PlayerMarker({p,meta,settings,selected,onSelect,localZ}){
  const pos=radarPos(meta,p.position),color=p.team===3?settings.ct:settings.t;
  const rotationRef=useRef(90-(Number(p.yaw)||0));
  const target=90-(Number(p.yaw)||0);
  rotationRef.current+=((target-rotationRef.current+540)%360)-180;
  if(!validNumber(pos.x)||!validNumber(pos.y)||pos.x<-4||pos.x>104||pos.y<-4||pos.y>104)return null;
  const hp=clamp(Number(p.health)||0,0,100),delta=validNumber(localZ)&&validNumber(p.position.z)?p.position.z-localZ:0;
  const floor=settings.elevation&&Math.abs(delta)>=90?(delta>0?'↑':'↓'):'';
  return <button aria-label={p.name} className={`player ${p.alive?'':'dead'} ${p.local?'local':''} ${selected?'selected':''}`} onClick={()=>onSelect(p.id)} style={{left:`${pos.x}%`,top:`${pos.y}%`,opacity:settings.opacity/100,'--c':color,'--size':`${settings.iconSize}px`,'--move-ms':`${clamp(190-settings.smoothing,45,190)}ms`,'--hp':`${hp*3.6}deg`}}>
    {settings.directions&&p.alive&&<i className="vision" style={{transform:`translate(-50%,-50%) rotate(${rotationRef.current}deg)`}}/>}
    <span className={`marker ${settings.healthRings?'ring':''}`}><i className="arrow" style={{transform:`rotate(${rotationRef.current}deg)`}}/></span>
    {floor&&<b className="floor">{floor}</b>}
    {settings.names&&<span className="player-name" style={{fontSize:settings.textSize}}>{p.local?'TU · ':''}{p.name}</span>}
  </button>;
}

function TeamPanel({team,title,players,settings,selected,select}){
  const color=team===3?settings.ct:settings.t,alive=players.filter(p=>p.alive).length;
  return <section className={`team-panel team-${team}`}><header><div><small>{team===3?'DEFESA':'ATAQUE'}</small><h2><i style={{background:color}}/>{title}</h2></div><strong>{alive}<span>/{players.length}</span></strong></header>
    <div className="roster">{players.map(p=><button className={`${selected===p.id?'active':''} ${p.alive?'':'eliminated'}`} key={p.id} onClick={()=>select(p.id)}><span className="avatar" style={{'--team':color}}>{(p.name||'?').slice(0,1).toUpperCase()}</span><span className="identity"><b>{p.name}{p.local?' (tu)':''}</b><small>{p.alive?(settings.weapons?cleanWeapon(p.weapon)||'SEM ARMA':'Em jogo'):'Eliminado'}</small></span>{settings.health&&<span className="vitals"><b>{clamp(p.health,0,100)}</b><i><em style={{width:`${clamp(p.health,0,100)}%`,background:color}}/></i></span>}</button>)}</div>
  </section>;
}

function SettingRange({label,value,min,max,step=1,suffix='',onChange}){return <label className="range"><span>{label}<b>{value}{suffix}</b></span><input type="range" min={min} max={max} step={step} value={value} onChange={e=>onChange(+e.target.value)}/></label>}

export default function App(){
  const net=useRadarSocket(),snap=net.snapshot;
  const [settings,setSettings]=useState(loadSettings),[open,setOpen]=useState(false),[selected,setSelected]=useState(''),[meta,setMeta]=useState(null);
  useEffect(()=>localStorage.setItem('omnighost.cs2.radar',JSON.stringify(settings)),[settings]);
  const map=snap?.map||'';
  useEffect(()=>{let cancelled=false;if(!MAPS.has(map)){setMeta(null);return}fetch(`/data/${map}/data.json`).then(r=>{if(!r.ok)throw 0;return r.json()}).then(v=>!cancelled&&setMeta(v)).catch(()=>!cancelled&&setMeta(null));return()=>{cancelled=true}},[map]);
  const players=useMemo(()=>snap?.players?.filter(p=>p&&p.position&&[2,3].includes(p.team))||[],[snap]);
  const terrorists=players.filter(p=>p.team===2),cts=players.filter(p=>p.team===3),chosen=players.find(p=>p.id===selected),local=players.find(p=>p.local);
  const aliveT=terrorists.filter(p=>p.alive).length,aliveCt=cts.filter(p=>p.alive).length;
  const update=(k,v)=>setSettings(s=>({...s,[k]:v}));
  return <div className="app">
    <header className="appbar"><div className="brand"><div className="logo"><span>OG</span></div><div><b>OMNIGHOST</b><small>TACTICAL WEB RADAR</small></div></div><div className="match-strip"><span className="team-score" style={{'--team':settings.t}}>T <b>{aliveT}</b></span><i>VS</i><span className="team-score" style={{'--team':settings.ct}}><b>{aliveCt}</b> CT</span></div><div className="top"><span className={`connection ${net.status}`}><i/>{LABELS[net.status]||net.status}</span><span className="map-name">{map?map.replace('de_','').toUpperCase():'SEM MAPA'}</span><span>{players.length} jogadores</span><span>{net.latency} ms</span><button aria-label="Definições" onClick={()=>setOpen(true)}>⚙</button></div></header>
    <main className={settings.panels?'with-panels':''}>{settings.panels&&<TeamPanel team={2} title="TERRORISTAS" players={terrorists} settings={settings} selected={selected} select={setSelected}/>}<section className="radar-shell"><div className="map-frame">
      {meta&&map?<div className="radar" style={{transform:`scale(${settings.zoom})`,backgroundImage:`url('/data/${map}/radar.png')`}}><div className="map-grid"/>{players.map(p=><PlayerMarker key={p.id} p={p} meta={meta} settings={settings} selected={selected===p.id} onSelect={setSelected} localZ={local?.position?.z}/>)}{settings.bomb&&snap.bomb?.position&&<div className="bomb" style={{left:`${radarPos(meta,snap.bomb.position).x}%`,top:`${radarPos(meta,snap.bomb.position).y}%`}}><span>C4</span></div>}</div>:<div className="empty"><div className="pulse-logo">OG</div><strong>{net.status==='connected'?'Mapa sem overview suportado':'A aguardar dados em direto'}</strong><span>{net.error||'Entra numa partida suportada no CS2.'}</span></div>}
    </div><div className="radar-info"><span><i className="live-dot"/>DADOS EM DIRETO</span><span>{map||'—'}</span><span>{players.length} ENTIDADES</span></div>{snap?.bomb?.planted&&<div className="bomb-status"><span>C4</span><div><small>BOMBA PLANTADA</small><b>{Number(snap.bomb.blowTime||0).toFixed(1)} s</b></div>{snap.bomb.defusing&&<div><small>A DESARMAR</small><b>{Number(snap.bomb.defuseTime||0).toFixed(1)} s</b></div>}</div>}</section>{settings.panels&&<TeamPanel team={3} title="CONTRATERRORISTAS" players={cts} settings={settings} selected={selected} select={setSelected}/>}</main>
    {chosen&&<aside className="details" style={{'--team':chosen.team===3?settings.ct:settings.t}}><button onClick={()=>setSelected('')}>×</button><div className="detail-head"><span>{(chosen.name||'?')[0]}</span><div><small>{chosen.team===3?'CONTRATERRORISTA':'TERRORISTA'}</small><h3>{chosen.name}</h3><p>{chosen.alive?'Em jogo':'Eliminado'}{chosen.local?' · jogador local':''}</p></div></div><div className="detail-grid"><span>Vida <b>{chosen.health}</b></span><span>Colete <b>{chosen.armor}</b></span><span>Arma <b>{cleanWeapon(chosen.weapon)||'—'}</b></span><span>Dinheiro <b>{chosen.money>=0?`$${chosen.money}`:'—'}</b></span></div><div className="coords">X {chosen.position.x.toFixed(0)} · Y {chosen.position.y.toFixed(0)} · Z {chosen.position.z.toFixed(0)}</div></aside>}
    {open&&<div className="modal" onMouseDown={e=>e.target===e.currentTarget&&setOpen(false)}><section><header><div><small>PERSONALIZAÇÃO</small><h2>Centro de controlo</h2><p>Ajusta a leitura do radar neste dispositivo.</p></div><button onClick={()=>setOpen(false)}>×</button></header><div className="settings"><div className="setting-group"><h3>INFORMAÇÃO</h3>{['names','health','weapons','directions','bomb','panels','elevation','healthRings'].map(k=><label className="toggle" key={k}><span>{{names:'Nomes dos jogadores',health:'Vida nas equipas',weapons:'Arma equipada',directions:'Direção e cone de visão',bomb:'Estado da bomba',panels:'Painéis das equipas',elevation:'Diferença de piso',healthRings:'Anel de vida'}[k]}</span><input type="checkbox" checked={settings[k]} onChange={e=>update(k,e.target.checked)}/></label>)}</div><div className="setting-group"><h3>APARÊNCIA</h3><SettingRange label="Tamanho dos marcadores" value={settings.iconSize} min={10} max={32} suffix=" px" onChange={v=>update('iconSize',v)}/><SettingRange label="Tamanho do texto" value={settings.textSize} min={9} max={18} suffix=" px" onChange={v=>update('textSize',v)}/><SettingRange label="Zoom do mapa" value={settings.zoom} min={.75} max={1.8} step={.05} suffix="×" onChange={v=>update('zoom',v)}/><SettingRange label="Opacidade" value={settings.opacity} min={30} max={100} suffix="%" onChange={v=>update('opacity',v)}/><SettingRange label="Suavização" value={settings.smoothing} min={0} max={100} suffix="%" onChange={v=>update('smoothing',v)}/><div className="colors"><label>Cor T<input type="color" value={settings.t} onChange={e=>update('t',e.target.value)}/></label><label>Cor CT<input type="color" value={settings.ct} onChange={e=>update('ct',e.target.value)}/></label></div></div></div><footer>As preferências ficam apenas neste navegador. Quando a ligação cai, posições e detalhes são removidos automaticamente.</footer></section></div>}
  </div>;
}
