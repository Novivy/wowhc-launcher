// WoW Hardcore Launcher — main UI component
// Communicates with native host via window.chrome.webview message bridge.

const T = {
  bg0: "#0a0806", bg1: "#14100a", bg2: "#1d1810", bg3: "#0a0e1a", plate: "#221a12", chrome: "#070504",
  line: "rgba(180,130,60,0.20)", line2: "rgba(180,130,60,0.10)",
  text: "#ecdab0", textDim: "#a08868", textFaint: "#6a5638", textFaint2: "#a58555",
  amber: "#e0a04a",  amber2: "#c3a173", lightBlue: "#498dbf", amberGlow: "rgba(224,160,74,0.5)", ember: "#c84a1a", blood: "#9a3422", fluid: "#228e9a",
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
        display: 'flex', alignItems: 'center', gap: 6, whiteSpace: 'nowrap', flexShrink: 0,
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
const BottomBar = ({ state, onAction, booting }) => {
  const { status, progress, installPath, isInstalled, isRecording, canSaveReplay, playEnabled, workerBusy, isLaunching, realmIndex } = state;
  const progressPct = Math.max(0, Math.min(100, progress || 0));
  const statusColor = booting ? T.textDim : (isInstalled ? T.amber2 : T.textDim);
  const ctrlDisabled = workerBusy || !isInstalled;
  const realmDisabled = ctrlDisabled || !!isLaunching;
  const [localRealm, setLocalRealm] = React.useState(realmIndex || 0);
  React.useEffect(function() { setLocalRealm(realmIndex || 0); }, [realmIndex]);

  return (
    <div style={{
      background: T.chrome, borderTop: '1px solid ' + T.line,
      padding: '20px', display: 'grid', gridTemplateColumns: '1fr 300px', gap: 21, alignItems: 'end',
      flexShrink: 0,
    }}>
      {/* Left column */}
      <div style={{ display: 'flex', flexDirection: 'column', minWidth: 0, height: 104, justifyContent: 'space-between' }}>
        {/* Action row */}
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          {React.createElement(ActionBtn, { icon: 'save',   iconColor: T.amber, label: 'Save Replay',    title: 'Save the current replay buffer to a video file', onClick: () => onAction('saveReplay'),         active: false, disabled: !canSaveReplay })}
          {React.createElement(ActionBtn, { icon: 'upload', iconColor: T.amber, label: 'Upload Replays', title: 'Upload recorded replays to Google Drive',         onClick: () => onAction('uploadReplays'),      active: false, disabled: ctrlDisabled })}
          <span style={{ flex: 1 }}/>
          {React.createElement(ActionBtn, { icon: 'cog',    iconColor: T.amber, label: '',               title: 'Video recording settings',                       onClick: () => onAction('openRecordSettings'), active: false, disabled: ctrlDisabled })}
          {React.createElement(ActionBtn, { icon: 'rec',    iconColor: T.blood,
            label: isRecording ? 'Stop Recording' : 'Start Recording',
            title: isRecording ? 'Stop the replay buffer recording' : 'Start recording your recent gameplay for replays',
            onClick: () => onAction(isRecording ? 'stopRecording' : 'startRecording'),
            active: isRecording, disabled: ctrlDisabled })}
        </div>

        {/* Path row */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 5, marginTop:7, minWidth: 0, justifyContent: 'flex-end' }}>
          <span style={{
            color: T.textFaint, fontFamily: 'ui-monospace, monospace', fontSize: 12,
            overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', paddingRight: 6,
          }}>{installPath || 'No installation path selected'}</span>
          {React.createElement(PathIconBtn, { title: 'Edit install path', onClick: () => onAction('browse'),     icon: 'pen',    disabled: workerBusy })}
          {React.createElement(PathIconBtn, { title: 'Open folder',       onClick: () => onAction('openFolder'), icon: 'folder', disabled: !installPath || workerBusy })}
        </div>

        {/* Status + progress bar */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
          <span style={{ color: statusColor, fontSize: 12, fontFamily: 'Inter, system-ui, sans-serif', fontWeight: 300, paddingLeft: 3 }}>
            {booting
              ? '◌ INITIALISING'
              : (isInstalled
                  ? ('● ' + (status || 'READY') + ' · ' + progressPct + '%')
                  : ('○ ' + (status || 'NOT INSTALLED')))}
          </span>
          <div style={{ position: 'relative', height: 8, background: '#0a0604', border: '1px solid ' + T.line2 }}>
            {booting && (
              <div style={{
                position: 'absolute', top: 0, bottom: 0, width: '45%',
                background: 'linear-gradient(90deg, transparent, ' + T.amber + ' 50%, transparent)',
                animation: 'wv-scan 1.3s linear infinite',
              }}/>
            )}
            {!booting && progressPct > 0 && (
              <div style={{
                position: 'absolute', top: 0, left: 0, bottom: 0, width: progressPct + '%',
                background: 'linear-gradient(90deg, ' + T.ember + ', ' + T.amber + ')',
                transition: 'width 300ms ease',
              }}/>
            )}
            <div style={{ position: 'absolute', inset: 0, background: 'repeating-linear-gradient(90deg, transparent 0 7px, rgba(0,0,0,0.18) 7px 0px)' }}/>
          </div>
        </div>
      </div>

      {/* Right column — realm + START GAME + cog */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <select
          value={localRealm}
          disabled={realmDisabled}
          onChange={function(e) { var idx = parseInt(e.target.value); setLocalRealm(idx); onAction('setRealm', { index: idx }); }}
          style={{
            appearance: 'none', WebkitAppearance: 'none', background: T.bg2,
            border: '1px solid ' + T.line, color: T.text, height: 35, boxSizing: 'border-box',
            padding: '0 27px 0 12px', fontSize: 12,
            cursor: realmDisabled ? 'not-allowed' : 'pointer', width: '100%',
            opacity: realmDisabled ? 0.35 : 1,
            backgroundImage: "url(\"data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'><path d='M1 1l4 4 4-4' stroke='%23a08868' fill='none' stroke-width='1.4'/></svg>\")",
            backgroundRepeat: 'no-repeat', backgroundPosition: 'right 8px center',
          }}>
          {REALMS.map(function(r, i) { return React.createElement('option', { key: i, value: i }, r); })}
        </select>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 47px', gap: 6 }}>
          {React.createElement(StartBtn, { label: (isInstalled || !installPath) ? 'START GAME' : 'INSTALL', onClick: () => onAction('startGame'), disabled: !playEnabled })}
          {React.createElement(CogBtn,   { onClick: () => onAction('openSettings'), disabled: ctrlDisabled })}
        </div>
      </div>
    </div>
  );
};

const StartBtn = ({ label, onClick, disabled }) => {
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
      {label}
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

// ── ANSI colour parser ─────────────────────────────────────────────────────────
const ANSI_16 = [
  '#000000','#800000','#008000','#808000','#000080','#800080','#008080','#c0c0c0',
  '#808080','#ff0000','#00ff00','#ffff00','#0000ff','#ff00ff','#00ffff','#ffffff',
];
function ansi256(idx) {
  if (idx < 16) return ANSI_16[idx];
  if (idx >= 232) { const v = 8 + (idx - 232) * 10; return `rgb(${v},${v},${v})`; }
  const n = idx - 16, r = Math.floor(n / 36), g = Math.floor((n % 36) / 6), b = n % 6;
  return `rgb(${r?55+r*40:0},${g?55+g*40:0},${b?55+b*40:0})`;
}
function parseAnsi(text) {
  const DEFAULT = '#c0c0c0';
  const segs = [];
  let color = DEFAULT, last = 0;
  const re = /\x1b\[([0-9;]*)m/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    if (m.index > last) segs.push({ text: text.slice(last, m.index), color });
    const codes = m[1].split(';').map(Number);
    for (let i = 0; i < codes.length; i++) {
      const c = codes[i];
      if (c === 0 || c === 39)            color = DEFAULT;
      else if (c >= 30 && c <= 37)        color = ANSI_16[c - 30];
      else if (c >= 90 && c <= 97)        color = ANSI_16[c - 90 + 8];
      else if (c === 38 && codes[i+1] === 5 && i+2 < codes.length) { color = ansi256(codes[i+2]); i += 2; }
      else if (c === 38 && codes[i+1] === 2 && i+4 < codes.length) { color = `rgb(${codes[i+2]},${codes[i+3]},${codes[i+4]})`; i += 4; }
    }
    last = m.index + m[0].length;
  }
  if (last < text.length) segs.push({ text: text.slice(last), color });
  return segs.length ? segs : [{ text, color: DEFAULT }];
}

// ── HermesProxy console overlay ────────────────────────────────────────────────
const ConsoleOverlay = React.forwardRef(function ConsoleOverlay({ lines }, ref) {
  return (
    <div style={{
      position: 'absolute', bottom: 0, left: 0, right: 0,
      height: '55%', background: 'rgba(0,0,0,0.96)',
      borderTop: '1px solid rgba(180,130,60,0.15)',
      display: 'flex', flexDirection: 'column', overflow: 'hidden',
      fontFamily: 'ui-monospace, Consolas, monospace', fontSize: 10, zIndex: 10,
    }}>
      <div style={{ padding: '4px 10px', borderBottom: '1px solid rgba(180,130,60,0.1)', color: T.textFaint, fontSize: 11, letterSpacing: '0.1em', flexShrink: 0 }}>
        HERMESPROXY OUTPUT
      </div>
      <div ref={ref} style={{ flex: 1, overflowY: 'auto', padding: '5px 10px' }}>
        {lines.map(function(l, i) {
          const segs = parseAnsi(l);
          return React.createElement('div', { key: i, style: { lineHeight: 1.35, whiteSpace: 'pre-wrap', wordBreak: 'break-all' } },
            segs.map(function(s, j) {
              return React.createElement('span', { key: j, style: { color: s.color } }, s.text);
            })
          );
        })}
      </div>
    </div>
  );
});

// ── Modal system ───────────────────────────────────────────────────────────────
const ModalOverlay = ({ children }) => (
  React.createElement('div', {
    style: {
      position: 'absolute', inset: 0, zIndex: 100,
      background: 'rgba(0,0,0,0.78)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
    }
  }, React.createElement('div', {
    style: {
      background: T.bg1, border: '1px solid ' + T.line,
      padding: '26px 26px 22px', width: 380,
      boxShadow: '0 12px 48px rgba(0,0,0,0.7)',
    }
  }, children))
);

const ModalTitle = ({ text }) => (
  React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 9, marginBottom: 14 } },
    React.createElement('span', { style: { width: 18, height: 1, background: T.amber, display: 'inline-block', flexShrink: 0 } }),
    React.createElement('span', { style: { fontFamily: '"Cinzel", Georgia, serif', fontSize: 13, fontWeight: 600, color: T.text, letterSpacing: '0.06em', textTransform: 'uppercase' } }, text))
);

const ModalBtn = ({ label, onClick, secondary }) => {
  var [hov, setHov] = React.useState(false);
  return React.createElement('button', {
    onClick: onClick,
    onMouseEnter: function() { setHov(true); },
    onMouseLeave: function() { setHov(false); },
    style: {
      height: 32, padding: '0 16px', fontFamily: 'inherit', cursor: 'pointer',
      background: secondary ? (hov ? T.bg2 : T.bg1) : (hov ? T.plate : T.bg2),
      border: '1px solid ' + (secondary
        ? (hov ? T.line : 'rgba(180,130,60,0.12)')
        : (hov ? 'rgba(180,130,60,0.45)' : T.line)),
      color: secondary ? T.textDim : T.text,
      fontSize: 10, letterSpacing: '0.1em', textTransform: 'uppercase', fontWeight: 600,
      transition: 'background 120ms, border-color 120ms, color 120ms',
    }
  }, label);
};

const RadioOption = ({ label, sub, selected, onClick }) => {
  var [hov, setHov] = React.useState(false);
  return React.createElement('div', {
    onClick: onClick,
    onMouseEnter: function() { setHov(true); },
    onMouseLeave: function() { setHov(false); },
    style: {
      padding: '9px 11px', cursor: 'pointer', marginBottom: 7,
      border: '1px solid ' + (selected ? 'rgba(180,130,60,0.45)' : (hov ? T.line : 'rgba(180,130,60,0.08)')),
      background: selected ? 'rgba(224,160,74,0.06)' : (hov ? 'rgba(180,130,60,0.04)' : 'transparent'),
      transition: 'background 120ms, border-color 120ms',
    }
  },
    React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 8 } },
      React.createElement('div', { style: {
        width: 11, height: 11, borderRadius: '50%', flexShrink: 0,
        border: '1.5px solid ' + (selected ? T.amber : T.textFaint),
        background: selected ? T.amber : 'transparent',
        display: 'grid', placeItems: 'center',
      } }, selected && React.createElement('div', { style: { width: 4, height: 4, borderRadius: '50%', background: T.bg0 } })),
      React.createElement('span', { style: { color: selected ? T.text : T.textDim, fontSize: 12, fontWeight: selected ? 600 : 400 } }, label)),
    sub && React.createElement('div', { style: { fontSize: 10, color: T.textFaint, marginTop: 3, paddingLeft: 19, fontFamily: 'Inter, system-ui, sans-serif', letterSpacing: '0.03em' } }, sub));
};

// PTR Dialog
const PTRModal = ({ onDismiss, onRequestAccess }) => (
  React.createElement(ModalOverlay, null,
    React.createElement(ModalTitle, { text: 'Player Testing Realm' }),
    React.createElement('p', { style: { margin: '0 0 18px', fontSize: 12, color: T.textDim, lineHeight: 1.65 } },
      'The Player Testing Realm (PTR) requires special access.',
      React.createElement('br'),
      'Click “Request Access” to apply on the WOW-HC website.'),
    React.createElement('div', { style: { display: 'flex', justifyContent: 'flex-end', gap: 8 } },
      React.createElement(ModalBtn, { label: 'Dismiss', onClick: onDismiss, secondary: true }),
      React.createElement(ModalBtn, { label: 'Request Access', onClick: onRequestAccess })))
);

// Install Mode Dialog
const InstallModeModal = ({ onChoice, onClose }) => {
  var [sel, setSel] = React.useState('new');
  return React.createElement(ModalOverlay, null,
    React.createElement(ModalTitle, { text: 'Get Started' }),
    React.createElement('p', { style: { margin: '0 0 12px', fontSize: 12, color: T.textDim, lineHeight: 1.55 } }, 'How would you like to get started?'),
    React.createElement(RadioOption, { label: 'New installation', sub: 'Download and install a fresh client (1.12 or 1.14)', selected: sel === 'new', onClick: function() { setSel('new'); } }),
    React.createElement(RadioOption, { label: 'Existing installation', sub: 'Point to an existing WoW folder (1.12 or 1.14)', selected: sel === 'existing', onClick: function() { setSel('existing'); } }),
    React.createElement('div', { style: { display: 'flex', justifyContent: 'space-between', marginTop: 40 } },
      React.createElement(ModalBtn, { label: 'Close', onClick: onClose, secondary: true }),
      React.createElement(ModalBtn, { label: 'Continue', onClick: function() { onChoice(sel); } })));
};

// Version Picker Dialog
const VersionPickerModal = ({ onChoice, onBack }) => {
  var [sel, setSel] = React.useState(114);
  return React.createElement(ModalOverlay, null,
    React.createElement(ModalTitle, { text: 'Choose Client Version' }),
    React.createElement('p', { style: { margin: '0 0 12px', fontSize: 12, color: T.textDim, lineHeight: 1.55 } }, 'Choose which WoW client version to install:'),
    React.createElement(RadioOption, { label: 'Modern 1.14.2', sub: 'Recommended', selected: sel === 114, onClick: function() { setSel(114); } }),
    React.createElement(RadioOption, { label: 'Vanilla 1.12.1', sub: 'Original vanilla client', selected: sel === 112, onClick: function() { setSel(112); } }),
    React.createElement('div', { style: { display: 'flex', justifyContent: 'space-between', marginTop: 40 } },
      React.createElement(ModalBtn, { label: 'Back', onClick: onBack, secondary: true }),
      React.createElement(ModalBtn, { label: 'Continue', onClick: function() { onChoice(sel); } })));
};

// ── Main App ───────────────────────────────────────────────────────────────────
const App = ({ isNative }) => {
  const [fallen,  setFallen]  = React.useState([]);
  const [zones,   setZones]   = React.useState({});
  const [online,  setOnline]  = React.useState(null);
  const [news,    setNews]    = React.useState(NEWS);
  const [booting, setBooting] = React.useState(isNative);
  const [heroHov, setHeroHov] = React.useState(false);
  const [statsCountdown, setStatsCountdown] = React.useState('10:00');

  React.useEffect(function() {
    var s = document.createElement('style');
    s.textContent = '@keyframes wv-scan{0%{left:-45%}100%{left:110%}}';
    document.head.appendChild(s);
    return function() { s.remove(); };
  }, []);

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
  const [modal, setModal] = React.useState(null);
  const consoleScrollRef = React.useRef(null);

  // Bridge: receive messages from C++ host
  React.useEffect(function() {
    if (!isNative) return;
    function handler(evt) {
      console.log('[bridge] evt.data type=' + typeof evt.data, evt.data);
      try {
        var msg = typeof evt.data === 'string' ? JSON.parse(evt.data) : evt.data;
        console.log('[bridge] msg.type=' + msg.type + ' installPath=' + msg.installPath + ' isInstalled=' + msg.isInstalled);
        if (msg.type === 'state')       { setAppState(function(s) { return Object.assign({}, s, msg); }); setBooting(false); }
        if (msg.type === 'showModal')   setModal(msg.modal);
        if (msg.type === 'hideModal')   setModal(null);
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
  }, [hermesLines, appState.showConsole]);

  // Fetch live server data (wow-hc.com /json/ serves Access-Control-Allow-Origin: *)
  React.useEffect(function() {
    var ts   = function() { return '_=' + Date.now(); };
    var base = 'https://wow-hc.com/json/';
    var qs   = function() { return '?api_version=126&front_realm=1&' + ts(); };
    var STATS_MS = 10 * 60 * 1000;
    var lastStatsAt = Date.now();

    fetch(base + 'areas.json' + qs())
      .then(function(r) { return r.json(); }).then(function(d) { setZones(d); }).catch(function(){});

    function fetchStats() {
      lastStatsAt = Date.now();
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
    var t1 = setInterval(fetchStats, STATS_MS);
    var t2 = setInterval(fetchNews,  60 * 60 * 1000);
    var t3 = setInterval(function() {
      var rem = Math.max(0, STATS_MS - (Date.now() - lastStatsAt));
      var m = Math.floor(rem / 60000);
      var s = Math.floor((rem % 60000) / 1000);
      setStatsCountdown((m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s);
    }, 1000);
    return function() { clearInterval(t1); clearInterval(t2); clearInterval(t3); };
  }, []);

  function onAction(action, extra) {
    if (isNative) { send(action, extra || {}); }
    else { console.log('action:', action, extra); }
  }

  var versions    = appState.versions || {};
  var showConsole = appState.showConsole;
  var versionRows = [
    { name: 'Launcher',    ver: versions.launcher || 'v0.0.0-dev' },
    { name: 'Addon',       ver: versions.addon    || '—'     },
    { name: 'Client',      ver: versions.client   || '—'     },
    { name: 'HermesProxy', ver: versions.hermes   || '—'     },
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
          <span title="Minimize" onClick={() => onAction('minimize')} style={{ width: 40, height: 33, display: 'grid', placeItems: 'center', color: T.textFaint, fontSize: 15, cursor: 'pointer', transition: 'background 0.15s, color 0.15s' }}
            onMouseEnter={function(e) { e.currentTarget.style.background = 'rgba(180,130,60,0.10)'; e.currentTarget.style.color = T.textDim; }}
            onMouseLeave={function(e) { e.currentTarget.style.background = 'transparent'; e.currentTarget.style.color = T.textFaint; }}
          >&#8212;</span>
          <span title="Close"    onClick={() => onAction('close')}    style={{ width: 40, height: 33, display: 'grid', placeItems: 'center', color: T.textFaint, fontSize: 15, cursor: 'pointer', transition: 'background 0.15s, color 0.15s' }}
            onMouseEnter={function(e) { e.currentTarget.style.background = 'rgba(180,60,60,0.20)'; e.currentTarget.style.color = T.blood; }}
            onMouseLeave={function(e) { e.currentTarget.style.background = 'transparent'; e.currentTarget.style.color = T.textFaint; }}
          >&#x2715;</span>
        </div>
      </div>

      {/* Body */}
      <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>

        {/* Left rail */}
        <div style={{
          width: 230, padding: '15px 12px 10px 12px', display: 'flex', flexDirection: 'column', gap: 12,
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

          <div style={{ display: 'flex', justifyContent: 'center', padding: '9px 0px 1px' }}>
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

          <div style={{ height: 1, background: 'linear-gradient(90deg, transparent, ' + T.amber + ', transparent)', opacity: 0.3 }}/>

          <div style={{ flex: 1 }}/>

          <div style={{ display: 'flex', justifyContent: 'center' }}>
            <div style={{ display: 'flex', flexDirection: 'column', width: 135, gap:8 }}>
              <a onClick={() => onAction('openUrl', { url: 'https://wow-hc.com/shop' })} style={{
                fontSize: 10, color: T.amber, textDecoration: 'none',
                letterSpacing: '0.14em', textTransform: 'uppercase', fontWeight: 700,
                textAlign: 'center', padding: '10px 10px', border: '1px solid ' + T.line,
                background: T.plate, cursor: 'pointer', display: 'block', transition: 'background 0.15s, border-color 0.15s',
              }}
                onMouseEnter={function(e) { e.currentTarget.style.background = 'rgba(180,130,60,0.10)'; e.currentTarget.style.borderColor = 'rgba(180,130,60,0.45)'; }}
                onMouseLeave={function(e) { e.currentTarget.style.background = T.plate; e.currentTarget.style.borderColor = T.line; }}
              >Shop</a>

              <a onClick={() => onAction('openUrl', { url: appState.clientType === 2 ? 'https://wow-hc.com/addons/vanilla' : 'https://wow-hc.com/addons/classic' })} style={{
                fontSize: 10, color: T.amber, textDecoration: 'none',
                letterSpacing: '0.14em', textTransform: 'uppercase', fontWeight: 700,
                textAlign: 'center', padding: '10px 10px', border: '1px solid ' + T.line,
                marginTop: -1, background: T.plate, cursor: 'pointer', display: 'block', transition: 'background 0.15s, border-color 0.15s',
              }}
                onMouseEnter={function(e) { e.currentTarget.style.background = 'rgba(180,130,60,0.10)'; e.currentTarget.style.borderColor = 'rgba(180,130,60,0.45)'; }}
                onMouseLeave={function(e) { e.currentTarget.style.background = T.plate; e.currentTarget.style.borderColor = T.line; }}
              >Addons</a>
            </div>
          </div>

          <a onClick={() => onAction('openWebsite')} style={{ fontSize: 11, color: T.textFaint2, fontFamily: 'ui-monospace, monospace', textDecoration:'underline', textAlign: 'center', letterSpacing: '0.1em', cursor: 'pointer', transition: 'color 0.15s' }}
            onMouseEnter={function(e) { e.currentTarget.style.color = T.textDim; }}
            onMouseLeave={function(e) { e.currentTarget.style.color = T.textFaint2; }}
          >
            wow-hc.com
          </a>
        </div>

        {/* Center */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', minWidth: 0, position: 'relative' }}>

          {/* Get Help — top right */}
          <a onClick={() => onAction('openGetHelp')} style={{
            position: 'absolute', top: 10, right: 12,
            fontSize: 11, color: T.textFaint2, fontFamily: 'ui-monospace, monospace',
            letterSpacing: '0.1em', textDecoration: 'underline', zIndex: 2, cursor: 'pointer', transition: 'color 0.15s',
          }}
            onMouseEnter={function(e) { e.currentTarget.style.color = T.textDim; }}
            onMouseLeave={function(e) { e.currentTarget.style.color = T.textFaint2; }}
          >Get Help</a>

          {/* Hero news strip */}
          {latestNews && (
            <a onClick={() => latestNews.slug && onAction('openUrl', { url: 'https://wow-hc.com/forums/' + latestNews.slug })}
              onMouseEnter={() => setHeroHov(true)}
              onMouseLeave={() => setHeroHov(false)}
              style={{
                display: 'block', textDecoration: 'none',
                cursor: latestNews.slug ? 'pointer' : 'default',
                padding: '26px 30px', borderBottom: '1px solid ' + T.line,
                background: heroHov
                  ? 'radial-gradient(ellipse at 80% 50%, rgba(224,160,74,0.18) 0%, transparent 60%), ' + T.bg1
                  : 'radial-gradient(ellipse at 80% 50%, rgba(224,160,74,0.10) 0%, transparent 60%), ' + T.bg1,
                flexShrink: 0, transition: 'background 0.2s',
              }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 5 }}>
                <span style={{ width: 22, height: 1, background: T.amber, display: 'inline-block' }}/>
                {SUB_CATEGORIES[latestNews.sub_category_id] &&
                  <span style={{ fontSize: 11, letterSpacing: '0.3em', color: T.amber, fontWeight: 700, textTransform: 'uppercase' }}>
                    {SUB_CATEGORIES[latestNews.sub_category_id].name}
                  </span>}
                <span style={{ fontSize:10, color: T.textFaint, fontFamily: 'Inter, system-ui, sans-serif', letterSpacing: '0.04em' }}>
                  {new Date(latestNews.created_at * 1000).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' })}
                </span>
              </div>
              <h1 style={{ margin: 0, fontFamily: '"Cinzel", Georgia, serif', fontSize: 16, fontWeight: 600, paddingTop: 2, color: heroHov ? T.amber : T.text, letterSpacing: '0.01em', lineHeight: 1.2, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', transition: 'color 0.2s' }}>
                {latestNews.title}
              </h1>
              <p style={{ margin: '2px 0 0', fontSize: 11, color: heroHov ? T.amber2 : T.textDim, lineHeight: 1.4, overflow: 'hidden', textOverflow: 'ellipsis', display: '-webkit-box', WebkitLineClamp: 1, WebkitBoxOrient: 'vertical', transition: 'color 0.2s' }}>
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
                  <span style={{ display: 'flex', alignItems: 'center', color: '#bdd1bb', fontSize: 11, fontFamily: 'Inter, system-ui, sans-serif', gap: 4 }} title="Players currently in-game">
                    {React.createElement('svg', { width: 11, height: 11, viewBox: '0 0 24 24' }, React.createElement('circle', { cx: 12, cy: 12, r: 7, fill: '#2CD90A' }))}
                    {online.toLocaleString()}
                    <span style={{ color: T.textFaint, fontSize: 10, letterSpacing: '0.08em' }}>In-game</span>
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
                    React.createElement('span', { style: { color: T.blood, fontFamily: 'Inter, system-ui, sans-serif', fontSize: 11, display: 'flex', gap: 3, flexShrink: 0 } },
                      React.createElement('span', { style: { color: T.textFaint, fontSize:11 } }, 'Lvl.'),
                      React.createElement('span', { style: { display: 'inline-block', width: '2ch', textAlign: 'right', fontSize:11 } }, f.level)),
                    React.createElement('span', { style: { overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' } },
                      React.createElement('span', { style: { color: CLASS_COLORS[f.class] || T.text, fontFamily: '"Cinzel", Georgia, serif', paddingLeft: 5 } }, f.name),
                      React.createElement('span', { style: { color: T.textFaint, fontSize: 11 } }, ' · ' + (zones[f.area] || ('Area ' + f.area)))),
                    React.createElement('span', { style: { color: T.textFaint, fontFamily: 'Inter, system-ui, sans-serif', fontSize: 11 } }, timeAgo(f.date)));
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
              <div style={{ padding: '5px 14px', borderBottom: '1px solid ' + T.line2, background: T.bg3, display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexShrink: 0 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 7 }}>
                  <div style={{ textAlign: 'left', fontFamily: 'Inter, system-ui, sans-serif', lineHeight: 1.3 }} title="Recent deaths and latest news are fetched from the server every 10 minutes">
                    <div style={{ fontSize: 9, color: T.textFaint, letterSpacing: '0.04em' }}>Auto-refresh in</div>
                    <div style={{ fontSize: 10, color: '#bdd1bb', letterSpacing: '0.04em' }}>{statsCountdown}</div>
                  </div>
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 7 }}>
                  <span style={{ fontSize: 11, letterSpacing: '0.14em', color: T.lightBlue, fontWeight: 700 }}>LATEST NEWS</span>
                  <img src="assets/icon.png" alt="" style={{ width: 14, height: 14, flexShrink: 0, objectFit: 'contain' }}/>
                </div>
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
                  onMouseEnter: function(e) { e.currentTarget.style.color = T.fluid; },
                  onMouseLeave: function(e) { e.currentTarget.style.color = T.textFaint; },
                }, 'Read More News')}
              </div>

              {showConsole && React.createElement(ConsoleOverlay, { lines: hermesLines, ref: consoleScrollRef })}
            </div>
          </div>
        </div>
      </div>

      {/* Bottom bar */}
      {React.createElement(BottomBar, { state: appState, onAction: onAction, booting: booting })}

      {/* Modal overlays */}
      {modal === 'ptr' && React.createElement(PTRModal, {
        onDismiss:       function() { setModal(null); onAction('ptrDismiss'); },
        onRequestAccess: function() { setModal(null); onAction('ptrRequestAccess'); },
      })}
      {modal === 'installMode' && React.createElement(InstallModeModal, {
        onChoice: function(choice) { setModal(null); onAction('installModeChoice', { choice: choice }); },
        onClose:  function()       { setModal(null); onAction('installModeClose'); },
      })}
      {modal === 'versionPicker' && React.createElement(VersionPickerModal, {
        onChoice: function(ver) { setModal(null); onAction('versionPickerChoice', { version: ver }); },
        onBack:   function()    { setModal(null); onAction('versionPickerBack'); },
      })}
    </div>
  );
};

window.App = App;
