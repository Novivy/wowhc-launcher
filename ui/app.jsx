// WoW Hardcore Launcher — main UI component
// Communicates with native host via window.chrome.webview message bridge.

const T = {
  bg0: "#0a0806", bg1: "#14100a", bg2: "#1d1810", bg3: "#0a0e1a", plate: "#221a12", chrome: "#070504",
  line: "rgba(180,130,60,0.20)", line2: "rgba(180,130,60,0.10)",
  text: "#ecdab0", textDim: "#a08868", textFaint: "#6a5638",
  amber: "#e0a04a", lightBlue: "#498dbf", amberGlow: "rgba(224,160,74,0.5)", ember: "#c84a1a", blood: "#9a3422", fluid: "#228e9a",
};

// ── Bridge helpers ─────────────────────────────────────────────────────────────
function send(action, extra) {
  if (window.chrome && window.chrome.webview)
    window.chrome.webview.postMessage(JSON.stringify({ action, ...extra }));
}

// ── SVG icon set (stroke-based, 24x24 viewbox) ────────────────────────────────
const Icon = ({ k, c, size }) => {
  c = c || 'currentColor'; size = size || 14;
  const p = { width: size, height: size, viewBox: '0 0 24 24', fill: 'none', stroke: c, strokeWidth: 1.8, strokeLinecap: 'round', strokeLinejoin: 'round', shapeRendering: 'geometricPrecision' };
  switch (k) {
    case 'rec':    return React.createElement('svg', p, React.createElement('circle', { cx: 12, cy: 12, r: 6, fill: c, stroke: 'none' }));
    case 'save':   return React.createElement('svg', p,
      React.createElement('path',     { d: 'M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z' }),
      React.createElement('polyline', { points: '17 21 17 13 7 13 7 21' }),
      React.createElement('polyline', { points: '7 3 7 8 15 8' }));
    case 'upload': return React.createElement('svg', p,
      React.createElement('path',     { d: 'M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4' }),
      React.createElement('polyline', { points: '17 8 12 3 7 8' }),
      React.createElement('line',     { x1: 12, y1: 3, x2: 12, y2: 15 }));
    case 'pen':    return React.createElement('svg', p,
      React.createElement('path', { d: 'M12 20h9' }),
      React.createElement('path', { d: 'M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4 12.5-12.5z' }));
    case 'folder': return React.createElement('svg', p,
      React.createElement('path', { d: 'M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z' }));
    case 'cog':    return React.createElement('svg', p,
      React.createElement('circle', { cx: 12, cy: 12, r: 3 }),
      React.createElement('path',   { d: 'M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z' }));
    default: return null;
  }
};

// ── Small reusable button components ──────────────────────────────────────────
const ActionBtn = ({ icon, iconColor, label, title, onClick, active, disabled }) => {
  const [hov, setHov] = React.useState(false);
  const isRec = icon === 'rec';
  const highlighted = !disabled && (hov || active);
  const recActive = isRec && active && !disabled;
  return (
    <button
      title={title}
      onClick={onClick}
      disabled={disabled}
      onMouseEnter={() => setHov(true)}
      onMouseLeave={() => setHov(false)}
      style={{
        height: 35, boxSizing: 'border-box',
        background: highlighted ? T.plate : T.bg2,
        border: '1px solid ' + (recActive ? T.blood : (highlighted ? 'rgba(180,130,60,0.45)' : T.line)),
        color: recActive ? T.blood : (highlighted ? T.text : T.textDim),
        padding: '0 10px', fontSize: 9, letterSpacing: '0.06em', textTransform: 'uppercase',
        fontFamily: 'inherit', cursor: disabled ? 'not-allowed' : 'pointer', fontWeight: 600,
        display: 'flex', alignItems: 'center', gap: 8, whiteSpace: 'nowrap', flexShrink: 0,
        transition: 'background 120ms, border-color 120ms, color 120ms',
        opacity: disabled ? 0.35 : 1,
      }}>
      {isRec
        ? React.createElement('svg', { width: 14, height: 14, viewBox: '0 0 24 24' },
            React.createElement('circle', { cx: 12, cy: 12, r: 7, fill: active ? T.blood : T.amber }))
        : React.createElement(Icon, { k: icon, c: iconColor, size: 14 })}
      {label}
    </button>
  );
};

const PathIconBtn = ({ title, onClick, icon, disabled }) => {
  const [hov, setHov] = React.useState(false);
  return (
    <button title={title} onClick={onClick}
      disabled={disabled}
      onMouseEnter={() => setHov(true)}
      onMouseLeave={() => setHov(false)}
      style={{
        flexShrink: 0, width: 25, height: 25, padding: 0,
        background: 'transparent', border: '1px solid ' + (!disabled && hov ? T.amber : T.line),
        color: !disabled && hov ? T.amber : T.textDim, cursor: disabled ? 'not-allowed' : 'pointer',
        display: 'grid', placeItems: 'center',
        transition: 'color 140ms, border-color 140ms',
        opacity: disabled ? 0.35 : 1,
      }}>
      {React.createElement(Icon, { k: icon, size: 12 })}
    </button>
  );
};

// ── Bottom bar ─────────────────────────────────────────────────────────────────
const BottomBar = ({ state, onAction }) => {
  const { status, progress, installPath, isInstalled, isRecording, realmIndex } = state;
  const progressPct = Math.max(0, Math.min(100, progress || 0));
  const statusColor = isInstalled ? T.amber : T.textDim;

  return (
    <div style={{
      background: T.chrome, borderTop: '1px solid ' + T.line,
      padding: '20px', display: 'grid', gridTemplateColumns: '1fr 300px', gap: 21, alignItems: 'end',
      flexShrink: 0,
    }}>
      {/* Left column */}
      <div style={{ display: 'flex', flexDirection: 'column', minWidth: 0, height: 104, justifyContent: 'space-between' }}>
        {/* Action row */}
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          {React.createElement(ActionBtn, { icon: 'save',   iconColor: T.amber, label: 'Save Replay',    title: 'Save the current replay buffer to a video file', onClick: () => onAction('saveReplay'),         active: false })}
          {React.createElement(ActionBtn, { icon: 'upload', iconColor: T.amber, label: 'Upload Replays', title: 'Upload recorded replays to Google Drive',         onClick: () => onAction('uploadReplays'),      active: false })}
          <span style={{ flex: 1 }}/>
          {React.createElement(ActionBtn, { icon: 'cog',    iconColor: T.amber, label: '',               title: 'Video recording settings',                              onClick: () => onAction('openRecordSettings'), active: false })}
          {React.createElement(ActionBtn, { icon: 'rec',    iconColor: T.blood,
            label: isRecording ? 'Stop Recording' : 'Start Recording',
            title: isRecording ? 'Stop the replay buffer recording' : 'Start recording your recent gameplay for replays',
            onClick: () => onAction(isRecording ? 'stopRecording' : 'startRecording'),
            active: isRecording })}
        </div>

        {/* Path row */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 5, marginTop:7, minWidth: 0, justifyContent: 'flex-end' }}>
          <span style={{
            color: T.textFaint, fontFamily: 'ui-monospace, monospace', fontSize: 12,
            overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', paddingRight: 6,
          }}>{installPath || 'No installation path selected'}</span>
          {React.createElement(PathIconBtn, { title: 'Edit install path', onClick: () => onAction('browse'),     icon: 'pen'    })}
          {React.createElement(PathIconBtn, { title: 'Open folder',       onClick: () => onAction('openFolder'), icon: 'folder' })}
        </div>

        {/* Status + progress bar */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
          <span style={{ color: statusColor, fontSize: 11, fontFamily: 'ui-monospace, monospace', fontWeight: 700, paddingLeft: 3 }}>
            {isInstalled
              ? ('● ' + (status || 'READY') + ' · ' + progressPct + '%')
              : ('○ ' + (status || 'NOT INSTALLED'))}
          </span>
          <div style={{ position: 'relative', height: 8, background: '#0a0604', border: '1px solid ' + T.line2 }}>
            {progressPct > 0 && (
              <div style={{
                position: 'absolute', top: 0, left: 0, bottom: 0, width: progressPct + '%',
                background: 'linear-gradient(90deg, ' + T.ember + ', ' + T.amber + ')',
                transition: 'width 300ms ease',
              }}/>
            )}
            <div style={{ position: 'absolute', inset: 0, background: 'repeating-linear-gradient(90deg, transparent 0 7px, rgba(0,0,0,0.18) 7px 8px)' }}/>
          </div>
        </div>
      </div>

      {/* Right column — realm + START GAME + cog */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <select
          value={realmIndex}
          onChange={function(e) { onAction('setRealm', { index: parseInt(e.target.value) }); }}
          style={{
            appearance: 'none', WebkitAppearance: 'none', background: T.bg2,
            border: '1px solid ' + T.line, color: T.text, height: 35, boxSizing: 'border-box',
            padding: '0 27px 0 12px', fontSize: 11, fontFamily: '"Cinzel", Georgia, serif',
            cursor: 'pointer', width: '100%',
            backgroundImage: "url(\"data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'><path d='M1 1l4 4 4-4' stroke='%23a08868' fill='none' stroke-width='1.4'/></svg>\")",
            backgroundRepeat: 'no-repeat', backgroundPosition: 'right 8px center',
          }}>
          {REALMS.map(function(r, i) { return React.createElement('option', { key: i, value: i }, r); })}
        </select>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 47px', gap: 6 }}>
          {React.createElement(StartBtn, { isInstalled: isInstalled, onClick: () => onAction('startGame') })}
          {React.createElement(CogBtn,   { onClick: () => onAction('openSettings') })}
        </div>
      </div>
    </div>
  );
};

const StartBtn = ({ isInstalled, onClick, disabled }) => {
  const [hov, setHov] = React.useState(false);
  return (
    <button onClick={onClick}
      disabled={disabled}
      onMouseEnter={() => setHov(true)} onMouseLeave={() => setHov(false)}
      style={{
        height: 63,
        background: 'linear-gradient(180deg, ' + T.amber + ' 0%, ' + T.ember + ' 100%)',
        border: '1px solid ' + T.amber,
        color: '#1a0a04', fontFamily: '"Cinzel", Georgia, serif',
        fontSize: 20, fontWeight: 700, letterSpacing: '0.2em', textTransform: 'uppercase',
        cursor: disabled ? 'not-allowed' : 'pointer', textShadow: '0 1px 0 rgba(255,220,160,0.4)',
        filter: !disabled && hov ? 'brightness(1.12)' : 'none', transition: 'filter 120ms',
        opacity: disabled ? 0.35 : 1,
      }}>
      {isInstalled ? 'START GAME' : 'INSTALL'}
    </button>
  );
};

const CogBtn = ({ onClick, disabled }) => {
  const [hov, setHov] = React.useState(false);
  return (
    <button onClick={onClick} title="Settings"
      disabled={disabled}
      onMouseEnter={() => setHov(true)} onMouseLeave={() => setHov(false)}
      style={{
        height: 63, background: !disabled && hov ? T.bg2 : T.plate,
        border: '1px solid ' + (!disabled && hov ? 'rgba(180,130,60,0.45)' : T.line),
        color: T.amber, cursor: disabled ? 'not-allowed' : 'pointer', display: 'grid', placeItems: 'center',
        transition: 'background 120ms, border-color 120ms',
        opacity: disabled ? 0.35 : 1,
      }}>
      {React.createElement(Icon, { k: 'cog', size: 20 })}
    </button>
  );
};

// ── HermesProxy console overlay ────────────────────────────────────────────────
const ConsoleOverlay = React.forwardRef(function ConsoleOverlay({ lines }, ref) {
  return (
    <div style={{
      position: 'absolute', bottom: 0, left: 0, right: 0,
      height: '55%', background: 'rgba(0,0,0,0.96)',
      borderTop: '1px solid rgba(180,130,60,0.15)',
      display: 'flex', flexDirection: 'column', overflow: 'hidden',
      fontFamily: 'ui-monospace, Consolas, monospace', fontSize: 12, zIndex: 10,
    }}>
      <div style={{ padding: '4px 10px', borderBottom: '1px solid rgba(180,130,60,0.1)', color: T.textFaint, fontSize: 11, letterSpacing: '0.1em', flexShrink: 0 }}>
        HERMESPROXY OUTPUT
      </div>
      <div ref={ref} style={{ flex: 1, overflowY: 'auto', padding: '5px 10px' }}>
        {lines.map(function(l, i) {
          return React.createElement('div', { key: i, style: { color: '#c0c0c0', lineHeight: 1.45, whiteSpace: 'pre-wrap', wordBreak: 'break-all' } }, l);
        })}
      </div>
    </div>
  );
});

// ── Main App ───────────────────────────────────────────────────────────────────
const App = ({ isNative }) => {
  const [fallen,  setFallen]  = React.useState([]);
  const [zones,   setZones]   = React.useState({});
  const [online,  setOnline]  = React.useState(null);
  const [news,    setNews]    = React.useState(NEWS);

  const [appState, setAppState] = React.useState({
    status: 'Select an installation folder',
    progress: 0,
    installPath: '',
    isInstalled: false,
    isRecording: false,
    canSaveReplay: false,
    realmIndex: 0,
    clientType: 0,
    versions: { launcher: '', hermes: '', addon: '', client: '' },
    showConsole: false,
  });
  const [hermesLines, setHermesLines] = React.useState([]);
  const consoleScrollRef = React.useRef(null);

  // Bridge: receive messages from C++ host
  React.useEffect(function() {
    if (!isNative) return;
    function handler(evt) {
      console.log('[bridge] evt.data type=' + typeof evt.data, evt.data);
      try {
        var msg = typeof evt.data === 'string' ? JSON.parse(evt.data) : evt.data;
        console.log('[bridge] msg.type=' + msg.type + ' installPath=' + msg.installPath + ' isInstalled=' + msg.isInstalled);
        if (msg.type === 'state')       setAppState(function(s) { return Object.assign({}, s, msg); });
        if (msg.type === 'hermesLine')  setHermesLines(function(prev) { return prev.slice(-500).concat([msg.text]); });
        if (msg.type === 'serverStats') {
          if (msg.data.last_deaths)            setFallen(msg.data.last_deaths);
          if (msg.data.online_players != null) setOnline(msg.data.online_players);
        }
        if (msg.type === 'areasData' && msg.data)  setZones(msg.data);
        if (msg.type === 'newsData'  && Array.isArray(msg.data) && msg.data.length) setNews(msg.data);
      } catch (e) { console.error('[bridge] parse error', e, evt.data); }
    }
    window.chrome.webview.addEventListener('message', handler);
    send('ready');
    return function() { window.chrome.webview.removeEventListener('message', handler); };
  }, [isNative]);

  // Auto-scroll console
  React.useEffect(function() {
    if (consoleScrollRef.current)
      consoleScrollRef.current.scrollTop = consoleScrollRef.current.scrollHeight;
  }, [hermesLines]);

  // Fetch live server data (wow-hc.com /json/ serves Access-Control-Allow-Origin: *)
  React.useEffect(function() {
    var ts   = function() { return '_=' + Date.now(); };
    var base = 'https://wow-hc.com/json/';
    var qs   = function() { return '?api_version=126&front_realm=1&' + ts(); };

    fetch(base + 'areas.json' + qs())
      .then(function(r) { return r.json(); }).then(function(d) { setZones(d); }).catch(function(){});

    function fetchStats() {
      fetch(base + 'server-stats.json' + qs())
        .then(function(r) { return r.json(); })
        .then(function(d) {
          if (d.last_deaths)            setFallen(d.last_deaths);
          if (d.online_players != null) setOnline(d.online_players);
        }).catch(function(){});
    }
    function fetchNews() {
      fetch(base + 'last-news.json' + qs())
        .then(function(r) { return r.json(); })
        .then(function(d) { if (Array.isArray(d) && d.length) setNews(d); })
        .catch(function(){});
    }

    fetchStats(); fetchNews();
    var t1 = setInterval(fetchStats, 10 * 60 * 1000);
    var t2 = setInterval(fetchNews,  60 * 60 * 1000);
    return function() { clearInterval(t1); clearInterval(t2); };
  }, []);

  function onAction(action, extra) {
    if (isNative) { send(action, extra || {}); }
    else { console.log('action:', action, extra); }
  }

  var versions    = appState.versions || {};
  var showConsole = appState.showConsole;
  var versionRows = [
    { name: 'Launcher',    ver: versions.launcher || 'v0.0.0-dev' },
    { name: 'HermesProxy', ver: versions.hermes   || '—'     },
    { name: 'Addon',       ver: versions.addon    || '—'     },
    { name: 'Client',      ver: versions.client   || '—'     },
  ];

  var latestNews = news[0];
  var olderNews  = news.slice(1);

  return (
    <div style={{
      width: 875, height: 530, display: 'flex', flexDirection: 'column',
      background: T.bg0, color: T.text, fontFamily: '"Inter", system-ui, sans-serif',
      border: '1px solid ' + T.line, position: 'relative', overflow: 'hidden',
    }}>

      {/* Title bar — always visible; acts as drag region in native mode */}
      <div className="html-chrome" style={{
        height: 33, background: T.chrome, borderBottom: '1px solid ' + T.line,
        display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 0 0 10px',
        flexShrink: 0,
      }}>
        {/* Draggable area — mousedown initiates native window drag */}
        <div
          onMouseDown={() => onAction('startDrag')}
          style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 9, fontSize: 11, letterSpacing: '0.18em', color: T.textDim, textTransform: 'uppercase', fontWeight: 600, cursor: 'default', userSelect: 'none', height: '100%' }}>
          <span style={{ width: 6, height: 6, borderRadius: '50%', background: T.amber, boxShadow: '0 0 8px ' + T.amberGlow }}/>
          WOW-HC Launcher
        </div>
        <div style={{ display: 'flex' }}>
          <span title="Minimize" onClick={() => onAction('minimize')} style={{ width: 40, height: 33, display: 'grid', placeItems: 'center', color: T.textFaint, fontSize: 15, cursor: 'pointer' }}>&#8212;</span>
          <span title="Close"    onClick={() => onAction('close')}    style={{ width: 40, height: 33, display: 'grid', placeItems: 'center', color: T.textFaint, fontSize: 15, cursor: 'pointer' }}>&#x2715;</span>
        </div>
      </div>

      {/* Body */}
      <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>

        {/* Left rail */}
        <div style={{
          width: 190, padding: '15px 12px 10px 12px', display: 'flex', flexDirection: 'column', gap: 12,
          background: 'linear-gradient(180deg, ' + T.bg1 + ' 0%, ' + T.bg0 + ' 100%)',
          borderRight: '1px solid ' + T.line, position: 'relative', flexShrink: 0,
        }}>
          {[0.08, 0.92].map(function(p) { return [0.08, 1.0].map(function(q) {
            return React.createElement('span', { key: p+'-'+q, style: {
              position: 'absolute', top: 'calc(' + (q*100) + '% - 17px)', left: 'calc(' + (p*100) + '% - 2px)',
              width: 5, height: 5, borderRadius: '50%',
              background: 'radial-gradient(circle at 30% 30%, #5a4628, #1a1208)',
              boxShadow: 'inset 0 0 0 0.5px rgba(0,0,0,0.5)',
            }});
          }); })}

          <div style={{ display: 'flex', justifyContent: 'center', padding: '3px 0' }}>
            <img src="assets/logo.png" alt="WoW Hardcore"
              style={{ width: 150, height: 'auto', filter: 'drop-shadow(0 2px 6px rgba(0,0,0,0.8))', imageRendering: 'auto' }}/>
          </div>

          <div style={{ height: 1, background: 'linear-gradient(90deg, transparent, ' + T.amber + ', transparent)', opacity: 0.3 }}/>

          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, fontFamily: 'ui-monospace, monospace', fontSize: 11 }}>
            {versionRows.map(function(v, i) {
              return React.createElement('div', { key: i, style: { display: 'flex', justifyContent: 'space-between', padding: '2px 5px', background: i % 2 ? 'transparent' : 'rgba(180,130,60,0.04)' } },
                React.createElement('span', { style: { color: T.textFaint } }, v.name),
                React.createElement('span', { style: { color: T.amber    } }, v.ver));
            })}
          </div>

          <div style={{ flex: 1 }}/>

          <a onClick={() => onAction('openGetAddons')} style={{
            fontSize: 10, color: T.amber, textDecoration: 'none',
            letterSpacing: '0.14em', textTransform: 'uppercase', fontWeight: 700,
            textAlign: 'center', padding: '10px 10px', border: '1px solid ' + T.line,
            background: T.plate, cursor: 'pointer', display: 'block',
          }}>Get More Addons</a>

          <a onClick={() => onAction('openWebsite')} style={{ fontSize: 11, color: T.textFaint, fontFamily: 'ui-monospace, monospace', textDecoration:'underline', textAlign: 'center', letterSpacing: '0.1em', cursor: 'pointer' }}>
            wow-hc.com
          </a>
        </div>

        {/* Center */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', minWidth: 0, position: 'relative' }}>

          {/* Get Help — top right */}
          <a onClick={() => onAction('openGetHelp')} style={{
            position: 'absolute', top: 10, right: 12,
            fontSize: 11, color: T.textFaint, fontFamily: 'ui-monospace, monospace',
            letterSpacing: '0.1em', textDecoration: 'underline', zIndex: 2, cursor: 'pointer',
          }}>Get Help</a>

          {/* Hero news strip */}
          {latestNews && (
            <a onClick={() => latestNews.slug && onAction('openUrl', { url: 'https://wow-hc.com/forums/' + latestNews.slug })}
              style={{
                display: 'block', textDecoration: 'none',
                cursor: latestNews.slug ? 'pointer' : 'default',
                padding: '23px 30px', borderBottom: '1px solid ' + T.line,
                background: 'radial-gradient(ellipse at 80% 50%, rgba(224,160,74,0.10) 0%, transparent 60%), ' + T.bg1,
                flexShrink: 0,
              }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 5 }}>
                <span style={{ width: 22, height: 1, background: T.amber, display: 'inline-block' }}/>
                {SUB_CATEGORIES[latestNews.sub_category_id] &&
                  <span style={{ fontSize: 11, letterSpacing: '0.3em', color: T.amber, fontWeight: 700, textTransform: 'uppercase' }}>
                    {SUB_CATEGORIES[latestNews.sub_category_id].name}
                  </span>}
                <span style={{ fontSize:10, color: T.textFaint, fontFamily: 'ui-monospace, monospace', letterSpacing: '0.04em' }}>
                  {new Date(latestNews.created_at * 1000).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' })}
                </span>
              </div>
              <h1 style={{ margin: 0, fontFamily: '"Cinzel", Georgia, serif', fontSize: 16, fontWeight: 600, paddingTop: 2, color: T.text, letterSpacing: '0.01em', lineHeight: 1.2, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {latestNews.title}
              </h1>
              <p style={{ margin: '2px 0 0', fontSize: 11, color: T.textDim, lineHeight: 1.4, overflow: 'hidden', textOverflow: 'ellipsis', display: '-webkit-box', WebkitLineClamp: 1, WebkitBoxOrient: 'vertical' }}>
                {stripHtml(latestNews.content_preview)}
              </p>
            </a>
          )}

          {/* Two-panel body */}
          <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '1fr 1fr', minHeight: 0 }}>

            {/* Recent Deaths */}
            <div style={{ borderRight: '1px solid ' + T.line, display: 'flex', flexDirection: 'column', minHeight: 0, overflow: 'hidden' }}>
              <div style={{ padding: '10px 14px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid ' + T.line2, background: 'rgba(154,52,34,0.08)', flexShrink: 0 }}>
                <span style={{ fontSize: 11, letterSpacing: '0.14em', color: T.blood, fontWeight: 700 }}>RECENT DEATHS</span>
                {online != null && (
                  <span style={{ display: 'flex', alignItems: 'center', color: '#bdd1bb', fontSize: 11, fontFamily: 'ui-monospace, monospace', gap: 4 }}>
                    {React.createElement('svg', { width: 11, height: 11, viewBox: '0 0 24 24' }, React.createElement('circle', { cx: 12, cy: 12, r: 7, fill: '#2CD90A' }))}
                    {online.toLocaleString()} ONLINE
                  </span>
                )}
              </div>
              <div style={{ flex: 1, overflowY: 'auto' }}>
                {fallen.map(function(f, i) {
                  return React.createElement('div', { key: i, style: {
                    padding: '5px 12px',
                    borderBottom: '1px solid ' + T.line2,
                    display: 'grid', gridTemplateColumns: 'auto 1fr auto', gap: 7, alignItems: 'baseline', fontSize: 12,
                  }},
                    React.createElement('span', { style: { color: T.blood, fontFamily: 'ui-monospace, monospace', fontSize: 11, display: 'flex', gap: 3, flexShrink: 0 } },
                      React.createElement('span', { style: { color: T.textFaint } }, 'Lvl.'),
                      React.createElement('span', { style: { display: 'inline-block', width: '2ch', textAlign: 'right' } }, f.level)),
                    React.createElement('span', { style: { overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' } },
                      React.createElement('span', { style: { color: CLASS_COLORS[f.class] || T.text, fontFamily: '"Cinzel", Georgia, serif', paddingLeft: 5 } }, f.name),
                      React.createElement('span', { style: { color: T.textFaint, fontSize: 11 } }, ' · ' + (zones[f.area] || ('Area ' + f.area)))),
                    React.createElement('span', { style: { color: T.textFaint, fontFamily: 'ui-monospace, monospace', fontSize: 11 } }, timeAgo(f.date)));
                })}
                {React.createElement('a', {
                  onClick: function() { onAction('openUrl', { url: 'https://wow-hc.com/leaderboard' }); },
                  style: {
                    display: 'flex', alignItems: 'center', justifyContent: 'center', height: 32, boxSizing: 'border-box',
                    fontSize: 9, letterSpacing: '0.14em', fontWeight: 700, textTransform: 'uppercase',
                    color: T.textFaint, textDecoration: 'none', cursor: 'pointer',
                    transition: 'color 120ms',
                  },
                  onMouseEnter: function(e) { e.currentTarget.style.color = T.blood; },
                  onMouseLeave: function(e) { e.currentTarget.style.color = T.textFaint; },
                }, 'View Leaderboard')}
              </div>
            </div>

            {/* Last News + optional HermesProxy console overlay */}
            <div style={{ display: 'flex', flexDirection: 'column', minHeight: 0, overflow: 'hidden', position: 'relative' }}>
              <div style={{ padding: '10px 14px', borderBottom: '1px solid ' + T.line2, background: T.bg3, display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: 7, flexShrink: 0 }}>
                <span style={{ fontSize: 11, letterSpacing: '0.14em', color: T.lightBlue, fontWeight: 700 }}>LATEST NEWS</span>
                <img src="assets/icon.png" alt="" style={{ width: 14, height: 14, flexShrink: 0, objectFit: 'contain' }}/>
              </div>
              <div style={{ flex: 1, overflowY: 'auto', padding: '0px 0' }}>
                {olderNews.map(function(n, i) {
                  return React.createElement('a', { key: i,
                    onClick: function() { if (n.slug) onAction('openUrl', { url: 'https://wow-hc.com/forums/' + n.slug }); },
                    onMouseEnter: function(e) { e.currentTarget.style.background = 'rgba(180,130,60,0.06)'; },
                    onMouseLeave: function(e) { e.currentTarget.style.background = 'transparent'; },
                    style: {
                      display: 'flex', flexDirection: 'column', gap: 3, textDecoration: 'none', minWidth: 0,
                      padding: '11px 17px',
                      borderBottom: '1px solid ' + T.line2,
                      cursor: 'pointer',
                      background: 'transparent', transition: 'background 120ms',
                    }},
                    React.createElement('span', { style: { fontSize: 12, color: T.text, lineHeight: 1.3, fontFamily: '"Cinzel", Georgia, serif', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' } }, n.title),
                    React.createElement('div', { style: { display: 'flex', gap: 5, fontSize: 9, color: T.textFaint, fontFamily: 'ui-monospace, monospace', letterSpacing: '0.04em', minWidth: 0, alignItems: 'flex-start' } },
                      React.createElement('div', { style: { display: 'flex', flexDirection: 'column', gap: 3, flexShrink: 0 } },
                        React.createElement('span', {}, new Date(n.created_at * 1000).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' })),
                        SUB_CATEGORIES[n.sub_category_id] && React.createElement('span', { style: { fontSize: 8, textAlign: 'center', color: T.amber, letterSpacing: '0.14em', fontWeight: 700, padding: '1px 4px', border: '1px solid ' + T.line, background: 'rgba(224,160,74,0.05)', textTransform: 'uppercase', fontFamily: '"Inter", system-ui, sans-serif' } },
                          SUB_CATEGORIES[n.sub_category_id].name)),
                      React.createElement('span', { style: { color: T.line, flexShrink: 0 } }, '·'),
                      React.createElement('span', { style: { overflow: 'hidden', display: '-webkit-box', WebkitLineClamp: 3, WebkitBoxOrient: 'vertical', color: T.textDim, fontFamily: '"Inter", system-ui, sans-serif', whiteSpace: 'normal' } }, stripHtml(n.content_preview))));
                })}
                {React.createElement('a', {
                  onClick: function() { onAction('openUrl', { url: 'https://wow-hc.com/forums' }); },
                  style: {
                    display: 'flex', alignItems: 'center', justifyContent: 'center', height: 32, boxSizing: 'border-box',
                    fontSize: 9, letterSpacing: '0.14em', fontWeight: 700, textTransform: 'uppercase',
                    color: T.textFaint, textDecoration: 'none', cursor: 'pointer',
                    transition: 'color 120ms',
                  },
                  onMouseEnter: function(e) { e.currentTarget.style.color = T.amber; },
                  onMouseLeave: function(e) { e.currentTarget.style.color = T.textFaint; },
                }, 'Read More News')}
              </div>

              {showConsole && React.createElement(ConsoleOverlay, { lines: hermesLines, ref: consoleScrollRef })}
            </div>
          </div>
        </div>
      </div>

      {/* Bottom bar */}
      {React.createElement(BottomBar, { state: appState, onAction: onAction })}
    </div>
  );
};

window.App = App;
