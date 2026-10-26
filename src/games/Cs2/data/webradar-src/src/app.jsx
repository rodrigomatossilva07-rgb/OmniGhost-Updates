import {useEffect,useMemo,useRef,useState} from 'react';

const DEFAULTS={names:true,health:true,weapons:true,directions:true,bomb:true,panels:true,iconSize:16,textSize:12,zoom:1,opacity:95,smoothing:70,fps:60,ct:'#45a7ff',t:'#f0b72f'};
const MAPS=new Set(['de_mirage','de_inferno','de_dust2','de_nuke','de_ancient','de_anubis','de_vertigo','de_overpass','de_train']);
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const validNumber=v=>Number.isFinite(Number(v));
const loadSettings=()=>{try{return {...DEFAULTS,...JSON.parse(localStorage.getItem('omnighost.cs2.radar')||'{}')}}catch{return DEFAULTS}};
const radarPos=(meta,p)=>({x:(p.x-meta.x)/(meta.scale*1024)*100,y:-((p.y-meta.y)/(meta.scale*1024))*100});
const token=location.hash.slice(1);

function useRadarSocket(){
  const [state,setState]=useState({status:'connecting',snapshot:null,latency:0,error:''});
  useEffect(()=>{let ws,timer,stale,stopped=false,attempt=0;
    const clear=()=>{clearTimeout(timer);clearTimeout(stale)};
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
    const retry=error=>{if(stopped)return;clear();setState(s=>({...s,status:'reconnecting',snapshot:null,error}));timer=setTimeout(connect,Math.min(8000,750*2**Math.min(attempt++,4)))};
    if(!token){setState({status:'error',snapshot:null,latency:0,error:'Token de acesso ausente'})}else connect();
    return()=>{stopped=true;clear();if(ws)ws.close()};
  },[]);return state;
}

function PlayerMarker({p,meta,settings,selected,onSelect}){
  const pos=radarPos(meta,p.position), color=p.team===3?settings.ct:settings.t;
  if(!validNumber(pos.x)||!validNumber(pos.y))return null;
  return <button className={`player ${p.alive?'':'dead'} ${selected?'selected':''} ${settings.directions?'':'no-direction'}`} onClick={()=>onSelect(p.id)} style={{left:`${pos.x}%`,top:`${pos.y}%`,opacity:settings.opacity/100,'--c':color,'--size':`${settings.iconSize}px`,transitionDuration:`${clamp(180-settings.smoothing,40,180)}ms`}}>
    <i style={{transform:settings.directions?`rotate(${270-(p.yaw||0)}deg)`:'none'}}/>{settings.names&&<span style={{fontSize:settings.textSize}}>{p.name}</span>}
  </button>
}

function Side({team,title,players,settings,select}){return <section className="team-panel"><h2><b style={{background:team===3?settings.ct:settings.t}}/>{title}<span>{players.length}</span></h2><div>{players.map(p=><button key={p.id} onClick={()=>select(p.id)}><strong>{p.name}</strong><small>{p.alive?(settings.health?`${p.health} HP · ${p.armor} AP`:'Vivo'):'Eliminado'}{settings.weapons&&p.weapon?` · ${p.weapon}`:''}</small></button>)}</div></section>}

export default function App(){
  const net=useRadarSocket(), snap=net.snapshot;
  const [settings,setSettings]=useState(loadSettings),[open,setOpen]=useState(false),[selected,setSelected]=useState(''),[meta,setMeta]=useState(null);
  useEffect(()=>localStorage.setItem('omnighost.cs2.radar',JSON.stringify(settings)),[settings]);
  const map=snap?.map||'';
  useEffect(()=>{let cancelled=false;if(!MAPS.has(map)){setMeta(null);return}fetch(`/data/${map}/data.json`).then(r=>{if(!r.ok)throw 0;return r.json()}).then(v=>!cancelled&&setMeta(v)).catch(()=>!cancelled&&setMeta(null));return()=>{cancelled=true}},[map]);
  const players=useMemo(()=>snap?.players?.filter(p=>p&&p.position&&[2,3].includes(p.team))||[],[snap]);
  const chosen=players.find(p=>p.id===selected);
  const update=(k,v)=>setSettings(s=>({...s,[k]:v}));
  return <div className="app">
    <header><div className="brand"><div className="logo">◇</div><div><b>OMNIGHOST</b><small>CS2 WEB RADAR</small></div></div><div className="top"><span className={`connection ${net.status}`}>{net.status}</span><span>{map||'Sem mapa'}</span><span>{players.length} jogadores</span><span>{net.latency} ms</span><button onClick={()=>setOpen(true)}>⚙</button></div></header>
    <main className={settings.panels?'with-panels':''}>
      {settings.panels&&<Side team={2} title="TERRORISTAS" players={players.filter(p=>p.team===2)} settings={settings} select={setSelected}/>} 
      <section className="radar-shell">
        {meta&&map?<div className="radar" style={{transform:`scale(${settings.zoom})`,backgroundImage:`url('/data/${map}/radar.png')`}}>
          {players.map(p=><PlayerMarker key={p.id} p={p} meta={meta} settings={settings} selected={selected===p.id} onSelect={setSelected}/>)}
          {settings.bomb&&snap.bomb?.position&&<div className="bomb" style={{left:`${radarPos(meta,snap.bomb.position).x}%`,top:`${radarPos(meta,snap.bomb.position).y}%`}}>C4</div>}
        </div>:<div className="empty"><strong>{net.status==='connected'?'Mapa sem overview suportado':'A aguardar dados em direto'}</strong><span>{net.error||'Abre uma partida suportada no CS2.'}</span></div>}
        {snap?.bomb?.planted&&<div className="bomb-status">BOMBA PLANTADA · {Number(snap.bomb.blowTime||0).toFixed(1)}s{snap.bomb.defusing?` · DEFUSE ${Number(snap.bomb.defuseTime||0).toFixed(1)}s`:''}</div>}
      </section>
      {settings.panels&&<Side team={3} title="CONTRATERRORISTAS" players={players.filter(p=>p.team===3)} settings={settings} select={setSelected}/>} 
    </main>
    {chosen&&<aside className="details"><button onClick={()=>setSelected('')}>×</button><h3>{chosen.name}</h3><p>{chosen.team===3?'CT':'T'} · {chosen.alive?'Vivo':'Eliminado'}</p><div><span>Vida <b>{chosen.health}</b></span><span>Colete <b>{chosen.armor}</b></span><span>Arma <b>{chosen.weapon||'—'}</b></span><span>Dinheiro <b>{chosen.money>=0?`$${chosen.money}`:'—'}</b></span></div></aside>}
    {open&&<div className="modal" onMouseDown={e=>e.target===e.currentTarget&&setOpen(false)}><section><header><div><small>PREFERÊNCIAS</small><h2>Definições do radar</h2></div><button onClick={()=>setOpen(false)}>×</button></header><div className="settings">
      {['names','health','weapons','directions','bomb','panels'].map(k=><label className="toggle" key={k}><span>{{names:'Nomes',health:'Vida',weapons:'Armas',directions:'Direções',bomb:'Bomba',panels:'Painéis laterais'}[k]}</span><input type="checkbox" checked={settings[k]} onChange={e=>update(k,e.target.checked)}/></label>)}
      <label>Tamanho dos ícones <input type="range" min="8" max="30" value={settings.iconSize} onChange={e=>update('iconSize',+e.target.value)}/></label><label>Tamanho do texto <input type="range" min="9" max="20" value={settings.textSize} onChange={e=>update('textSize',+e.target.value)}/></label><label>Zoom <input type="range" min="0.75" max="1.8" step=".05" value={settings.zoom} onChange={e=>update('zoom',+e.target.value)}/></label><label>Opacidade <input type="range" min="25" max="100" value={settings.opacity} onChange={e=>update('opacity',+e.target.value)}/></label><label>Suavização <input type="range" min="0" max="100" value={settings.smoothing} onChange={e=>update('smoothing',+e.target.value)}/></label><label>Cor T <input type="color" value={settings.t} onChange={e=>update('t',e.target.value)}/></label><label>Cor CT <input type="color" value={settings.ct} onChange={e=>update('ct',e.target.value)}/></label>
    </div><p>As preferências são guardadas apenas neste navegador. Dados antigos são removidos automaticamente quando a ligação é interrompida.</p></section></div>}
  </div>
}
