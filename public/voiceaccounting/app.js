(function(){
  // Minimal install prompt handling
  var deferredPrompt = null;
  var installBtn = document.getElementById('installBtn');
  window.addEventListener('beforeinstallprompt', function(e){
    e.preventDefault();
    deferredPrompt = e;
    installBtn.hidden = false;
  });
  installBtn && installBtn.addEventListener('click', function(){
    if (!deferredPrompt) return;
    deferredPrompt.prompt();
    deferredPrompt = null;
    installBtn.hidden = true;
  });

  // Helpers
  function $(id){ return document.getElementById(id); }
  function setText(id, s){ var el=$(id); if(el) el.textContent = s || ''; }

  // Default date = today
  (function(){
    var d = new Date();
    var iso = new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,10);
    $('date').value = iso;
  })();

  var token = null;

  function postJSON(path, obj){
    return fetch(path, { method:'POST', headers:{ 'Content-Type':'application/json' }, body: JSON.stringify(obj) })
      .then(function(r){ if(!r.ok) throw new Error('HTTP '+r.status); return r.json(); });
  }

  // Auth
  $('btnLogin').addEventListener('click', function(){
    var u=$('username').value.trim(); var p=$('password').value;
    setText('authStatus','Logging in...');
    postJSON('/api/login', { username:u, password:p })
      .then(function(res){ token = res.token; setText('authStatus','Logged in'); })
      .catch(function(err){ setText('authStatus','Login failed: '+err.message); });
  });
  $('btnSignup').addEventListener('click', function(){
    var u=$('username').value.trim(); var p=$('password').value;
    setText('authStatus','Signing up...');
    postJSON('/api/signup', { username:u, password:p })
      .then(function(){ setText('authStatus','Signed up. Now login.'); })
      .catch(function(err){ setText('authStatus','Signup failed: '+err.message); });
  });

  // Voice recognition (uses browser SpeechRecognition; for Google Cloud STT, integrate via proxy)
  var SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  var rec = SR ? new SR() : null;
  if (rec) {
    rec.lang = 'en-US'; rec.interimResults = true; rec.continuous = false;
    rec.onresult = function(e){
      var s='';
      for (var i=e.resultIndex; i<e.results.length; i++) s += e.results[i][0].transcript;
      $('transcript').value = s.trim();
    };
    rec.onstart = function(){ setText('voiceStatus','Listening...'); $('btnVoice').textContent='Stop Voice'; };
    rec.onend = function(){ setText('voiceStatus',''); $('btnVoice').textContent='Start Voice'; };
    rec.onerror = function(e){ setText('voiceStatus','Error: '+e.error); };
  } else {
    setText('voiceStatus','SpeechRecognition not supported in this browser');
  }

  $('btnVoice').addEventListener('click', function(){
    if (!rec) return;
    try {
      if ($('btnVoice').textContent.indexOf('Start')===0) rec.start(); else rec.stop();
    } catch (e) { setText('voiceStatus','Error: '+e.message); }
  });

  // Parse transcript into lines array
  function parseLinesFromTranscript(t){
    // Expected patterns (case-insensitive):
    //  "debit 120 to 5300", "credit 120 to 1000", joined with "and", commas, or periods
    var out=[];
    if (!t) return out;
    var s=t.toLowerCase();
    // normalize separators
    s=s.replace(/\./g,' and ').replace(/,/g,' and ').replace(/\s+/g,' ').trim();
    var parts=s.split(/\band\b/);
    for (var i=0;i<parts.length;i++){
      var p = parts[i].trim(); if(!p) continue;
      var m = p.match(/\b(debit|credit)\s+([0-9]+(?:\.[0-9]+)?)\s+(?:to|into|on|in)\s+([a-z0-9_-]{3,})/);
      if (m){
        var kind=m[1]; var amt=parseFloat(m[2]); var acct=m[3].toUpperCase();
        out.push({ account_code: acct, debit: kind==='debit'? amt:0, credit: kind==='credit'? amt:0 });
      }
    }
    return out;
  }

  function renderLines(lines){
    var root=$('lines'); root.innerHTML='';
    if (!lines.length){ root.innerHTML='<div class="status">No lines parsed yet.</div>'; return; }
    for (var i=0;i<lines.length;i++){
      var l=lines[i];
      var row=document.createElement('div'); row.className='line';
      row.innerHTML=
        '<label>Account <input value="'+(l.account_code||'')+'" data-k="account_code"/></label>'+
        '<label>Debit <input value="'+(l.debit||0)+'" data-k="debit" type="number" step="0.01"/></label>'+
        '<label>Credit <input value="'+(l.credit||0)+'" data-k="credit" type="number" step="0.01"/></label>'+
        '<button data-act="del">✕</button>';
      row.addEventListener('input', function(ev){
        var k=ev.target.getAttribute('data-k'); if(!k) return;
        var idx=[].indexOf.call(root.children, this); if(idx<0) return;
        if (k==='account_code') lines[idx][k]=ev.target.value.toUpperCase();
        else lines[idx][k]=parseFloat(ev.target.value||0);
      });
      row.querySelector('button[data-act="del"]').addEventListener('click', (function(idx){ return function(){ lines.splice(idx,1); renderLines(lines); };})(i));
      root.appendChild(row);
    }
  }

  var currentLines=[];

  $('btnParse').addEventListener('click', function(){
    var t=$('transcript').value;
    currentLines = parseLinesFromTranscript(t);
    if (!currentLines.length) setText('parseStatus','No lines recognized. Try: "debit 120 to 5300 and credit 120 to 1000"');
    else setText('parseStatus','Parsed '+currentLines.length+' line(s)');
    renderLines(currentLines);
  });

  $('btnSubmit').addEventListener('click', function(){
    if (!token) { setText('submitStatus','Please login first'); return; }
    var projectId=$('projectId').value.trim();
    if (!projectId){ setText('submitStatus','Project ID required'); return; }
    var date=$('date').value; var memo=$('memo').value||'';
    if (!currentLines.length){ setText('submitStatus','No lines to submit'); return; }
    setText('submitStatus','Posting...');
    postJSON('/api/postJournal', { token: token, project_id: projectId, date: date, memo: memo, lines: currentLines })
      .then(function(res){ setText('submitStatus','Posted entry '+(res.entry && res.entry.id ? res.entry.id : 'OK')); currentLines=[]; renderLines(currentLines); })
      .catch(function(err){ setText('submitStatus','Post failed: '+err.message); });
  });
})();

