(function(){
const meta = window.__APP_META__ || {endpoints:[]};
const eps = meta.endpoints;
const list = document.getElementById('endpoints');
let selected = eps.length ? eps[0].name : '';
function render(){ list.innerHTML=''; eps.forEach(ep=>{ const b=document.createElement('button'); b.textContent=ep.name; b.onclick=()=>{selected=ep.name;}; list.appendChild(b); }); }
render();
document.getElementById('runBtn').onclick = async function(){
  const token = document.getElementById('token').value.trim();
  let params = {}; try { params = JSON.parse(document.getElementById('params').value||'{}'); } catch(e){ alert('Invalid JSON in params'); return; }
  if (token) params.token = token;
  const res = await fetch('/api/'+selected, { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(params)});
  const txt = await res.text();
  document.getElementById('out').textContent = txt;
};
})();
