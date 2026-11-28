// Synthetic Chromium controls on the test's private desktop, never a user profile.
const { app, BrowserWindow, screen } = require('electron');
const fs = require('fs');
const path = require('path');
const out = process.argv[2];
app.setPath('userData', path.join(out, 'profile'));
app.commandLine.appendSwitch('disable-gpu');
app.commandLine.appendSwitch('force-renderer-accessibility');
app.whenReady().then(async () => {
  app.setAccessibilitySupportEnabled(true);
  const win = new BrowserWindow({ width: 900, height: 600, x: 100, y: 100, frame: false,
    show: false, webPreferences: { contextIsolation: true } });
  await win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(`
    <html><style>body{font:18px Arial;margin:20px} #source{width:380px;white-space:pre-wrap}
    #terminal{position:absolute;left:490px;top:80px;width:300px;height:260px;background:#ddd}
    .bar{position:absolute;width:16px;height:90px;top:80px;background:#777;user-select:none}
    #bar{left:400px} #plain{left:440px} #split{left:470px}
    #hidden{position:absolute;left:-10000px;top:-10000px}</style>
    <p id="source">Selected text stays in its own editor.</p>
    <div id="bar" class="bar" role="scrollbar" tabindex="0" aria-valuenow="10" aria-valuemin="0" aria-valuemax="100"></div>
    <div id="plain" class="bar"></div><div id="split" class="bar" role="separator"></div>
    <div id="terminal" tabindex="0">Terminal output with no selection.</div><textarea id="hidden">Hidden input buffer</textarea></html>`));
  win.showInactive();
  let busy = false, previous = -1;
  const timer = setInterval(async () => {
    if (fs.existsSync(path.join(out, 'stop'))) { clearInterval(timer); app.quit(); return; }
    if (busy || !fs.existsSync(path.join(out, 'request.json'))) return;
    const request = JSON.parse(fs.readFileSync(path.join(out, 'request.json')));
    if (request.id === previous) return;
    busy = true; previous = request.id;
    try {
      const result = await win.webContents.executeJavaScript(`(() => {
        const mode=${JSON.stringify(request.mode)}, source=document.querySelector('#source'), s=getSelection();
        document.querySelector('#terminal').focus();s.removeAllRanges();
        source.textContent=mode==='long' ? Array.from({length:1500},(_,i)=>'Line '+i+' retains complete selected text.').join('\\n') : 'Selected text stays in its own editor.';
        if(mode==='hidden'){document.querySelector('#hidden').focus();document.querySelector('#hidden').select();}
        else if(mode!=='none'){
          const node=source.firstChild;
          if(mode==='reverse')s.setBaseAndExtent(node,node.length,node,0);
          else s.setBaseAndExtent(node,0,node,node.length);
        }
        const range=document.createRange();range.setStart(source.firstChild,0);range.setEnd(source.firstChild,1);
        const b=range.getBoundingClientRect(), center=id=>{const r=document.querySelector(id).getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2];};
        return {expected:mode==='none'||mode==='hidden'?'':s.toString(),dpr:devicePixelRatio,
          points:{text:[b.left+b.width/2,b.top+b.height/2],scrollbar:center('#bar'),plainScrollbar:center('#plain'),separator:center('#split'),terminal:center('#terminal')}};
      })()`);
      const bounds=win.getContentBounds();
      result.points=Object.fromEntries(Object.entries(result.points).map(([name,p])=>{
        const physical=screen.dipToScreenPoint({x:Math.round(bounds.x+p[0]),y:Math.round(bounds.y+p[1])});
        return [name,[physical.x,physical.y]];
      }));
      fs.writeFileSync(path.join(out, 'response.json'), JSON.stringify({ id: request.id,
        target: win.getNativeWindowHandle().readBigUInt64LE().toString(), ...result }));
    } finally { busy = false; }
  }, 40);
  fs.writeFileSync(path.join(out, 'ready'), 'ready');
  setTimeout(() => app.quit(), 120000);
});
