// A real Chromium TextPattern behind an unrelated focused input.
const { app, BrowserWindow } = require('electron');
const fs = require('fs');
const path = require('path');
const output = process.argv[2];
app.setPath('userData', path.join(output, 'profile'));
app.commandLine.appendSwitch('disable-gpu');
app.whenReady().then(async () => {
  const win = new BrowserWindow({ width: 800, height: 420, x: 100, y: 100,
    show: false, webPreferences: { contextIsolation: true } });
  await win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(
    '<html><body><h1>Selection fixture</h1><div><section><article>' +
    '<p id="source">Please translate this Chromium selection.</p>' +
    '</article></section></div><input value="Unrelated focus"></body></html>'
  ));
  win.showInactive();
  const expected = await win.webContents.executeJavaScript(`
    document.querySelector('input').focus();
    const range = document.createRange();
    range.selectNodeContents(document.getElementById('source'));
    const selection = window.getSelection();
    selection.removeAllRanges(); selection.addRange(range); selection.toString();
  `);
  fs.writeFileSync(path.join(output, 'browser.json'), JSON.stringify({
    target: win.getNativeWindowHandle().readBigUInt64LE().toString(), expected
  }));
  setInterval(() => { if (fs.existsSync(path.join(output, 'stop'))) app.quit(); }, 100);
  setTimeout(() => app.quit(), 40000);
});
