const macRe=/^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$/;
const $ = (id) => document.getElementById(id);

$('togglePwd')?.addEventListener('click', (e) => {
  e.preventDefault();
  const p=$('password'); if (p) p.type = p.type==='password' ? 'text' : 'password';
});

async function load(){
  try{
    const r=await fetch('/api/config');
    const j=await r.json();
    $('serverId').textContent = j.server?.serverId || '—';
    $('ssid').value           = j.wifi?.ssid || '';
    $('userLogin').value      = j.mqtt?.userLogin || '';
    $('staStatus').textContent = j.wifi?.connected ? 'Подключен' : 'Не подключен';
    $('staStatus').className   = j.wifi?.connected ? 'ok' : 'bad';
    $('staSsid').textContent = j.wifi?.staSsid || '—';
    $('staIp').textContent   = j.wifi?.ip || '—';
    $('macStaAct').textContent = j.mac?.activeSta || '—';
    $('macApAct').textContent  = j.mac?.activeAp || '—';
    $('macSta').value = j.mac?.savedSta || '';
    $('macAp').value  = j.mac?.savedAp || '';
  }catch(e){ console.error(e); }
}

async function wifiConnect(){
  const btn=$('btnWifi'); btn.disabled=true;
  const m=$('msgWifi'); m.textContent='';
  const ssid=$('ssid').value.trim();
  const password=$('password').value;
  if(!ssid){ m.textContent='Укажите SSID'; btn.disabled=false; return; }
  try{
    const r=await fetch('/api/wifi_connect',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({ssid,password})
    });
    const j=await r.json();
    m.textContent=(j.connected?'OK: IP получен':'Не удалось подключиться')+(j.connectReason?(' ('+j.connectReason+')'):'');
    await load();
  }catch(e){ m.textContent='Ошибка: '+e; }
  finally{ btn.disabled=false; }
}

async function saveUser(){
  const btn=$('btnUser'); btn.disabled=true;
  const m=$('msgUser'); m.textContent='';
  const userLogin=$('userLogin').value.trim();
  if(!userLogin){ m.textContent='Укажите userLogin'; btn.disabled=false; return; }
  try{
    const r=await fetch('/api/mqtt_user',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({userLogin})
    });
    const j=await r.json();
    m.textContent=(j.ok?'Сохранено':'Ошибка')+(j.err?(' '+j.err):'');
  }catch(e){ m.textContent='Ошибка: '+e; }
  finally{ btn.disabled=false; }
}

async function saveMac(){
  const btn=$('btnMac'); btn.disabled=true;
  const m=$('msgMac'); m.textContent='';
  const macSta=$('macSta').value.trim();
  const macAp =$('macAp').value.trim();

  if(macSta && !macRe.test(macSta)){ m.textContent='Неверный формат MAC STA'; btn.disabled=false; return; }
  if(macAp  && !macRe.test(macAp )){ m.textContent='Неверный формат MAC AP' ; btn.disabled=false; return; }

  try{
    const r = await fetch('/api/mac', {
      method:'POST', // если на сервере включили PUT — можно заменить на 'PUT'
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({ staMac: macSta, apMac: macAp })
    });

    // Сервер иногда отдает текст ошибки (405/400) вместо JSON → парсим безопасно
    const text = await r.text();
    let j = null;
    try { j = JSON.parse(text); } catch(_) {}

    if (!j) { m.textContent = `HTTP ${r.status}: ${text}`; return; }

    m.textContent = j.message || 'OK';
    if (j.reboot) m.textContent += ' Перезагрузка…';
  }catch(e){
    m.textContent='Ошибка: '+e;
  }finally{
    btn.disabled=false;
  }
}

$('btnWifi')?.addEventListener('click', wifiConnect);
$('btnUser')?.addEventListener('click', saveUser);
$('btnMac') ?.addEventListener('click', saveMac);

document.addEventListener('DOMContentLoaded', load);
