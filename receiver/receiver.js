/*
 * Lumen TV – receiver for the stream Lumen sends (docs/tv-protocol.md).
 *
 * Runs in three places with the same code:
 *   - in any browser, served by Lumen itself (http://<pc>:47800/)
 *   - in the packaged TV apps (Tizen, webOS), which add platform.js
 *   - in test runs
 * Plain ES5 on purpose: TV browsers are often many years old.
 */
(function () {
  'use strict';

  var PORT = 47800;
  var P = window.LumenPlatform || {};
  // Served by Lumen: the server is where this page came from
  var embedded = !P.name && /^https?:$/.test(location.protocol);

  var STR = {
    en: { searching: 'Searching for Lumen on this network…', notFound: 'Lumen was not found.',
          enter: 'Open “Cast” in Lumen on your computer and enter the address shown there.',
          connect: 'Connect', search: 'Search again', connecting: 'Connecting to %1…',
          ready: 'Connected to Lumen on %1', choose: 'In Lumen, open “Cast” and choose “%1”.',
          name: 'Name of this device', disconnect: 'Disconnect', lost: 'The connection to Lumen was lost.',
          failed: 'The stream cannot be played on this device.', sound: 'Press OK or click for sound' },
    de: { searching: 'Lumen wird in diesem Netz gesucht …', notFound: 'Lumen wurde nicht gefunden.',
          enter: 'In Lumen am Computer „Übertragen“ öffnen und die dort gezeigte Adresse eingeben.',
          connect: 'Verbinden', search: 'Erneut suchen', connecting: 'Verbindung zu %1 …',
          ready: 'Mit Lumen auf %1 verbunden', choose: 'In Lumen „Übertragen“ öffnen und „%1“ wählen.',
          name: 'Name dieses Geräts', disconnect: 'Trennen', lost: 'Die Verbindung zu Lumen ist abgebrochen.',
          failed: 'Der Strom lässt sich auf diesem Gerät nicht abspielen.', sound: 'OK drücken oder klicken für Ton' },
    fr: { searching: 'Recherche de Lumen sur ce réseau…', notFound: 'Lumen est introuvable.',
          enter: 'Ouvrez « Diffuser » dans Lumen sur votre ordinateur et saisissez l’adresse affichée.',
          connect: 'Connecter', search: 'Rechercher à nouveau', connecting: 'Connexion à %1…',
          ready: 'Connecté à Lumen sur %1', choose: 'Dans Lumen, ouvrez « Diffuser » et choisissez « %1 ».',
          name: 'Nom de cet appareil', disconnect: 'Déconnecter', lost: 'La connexion à Lumen a été perdue.',
          failed: 'Le flux ne peut pas être lu sur cet appareil.', sound: 'Appuyez sur OK ou cliquez pour le son' },
    es: { searching: 'Buscando Lumen en esta red…', notFound: 'No se encontró Lumen.',
          enter: 'Abre «Transmitir» en Lumen en tu ordenador e introduce la dirección que aparece.',
          connect: 'Conectar', search: 'Buscar de nuevo', connecting: 'Conectando con %1…',
          ready: 'Conectado a Lumen en %1', choose: 'En Lumen, abre «Transmitir» y elige «%1».',
          name: 'Nombre de este dispositivo', disconnect: 'Desconectar', lost: 'Se perdió la conexión con Lumen.',
          failed: 'La transmisión no se puede reproducir en este dispositivo.', sound: 'Pulsa OK o haz clic para el sonido' },
    it: { searching: 'Ricerca di Lumen in questa rete…', notFound: 'Lumen non è stato trovato.',
          enter: 'Apri «Trasmetti» in Lumen sul computer e inserisci l’indirizzo mostrato.',
          connect: 'Connetti', search: 'Cerca di nuovo', connecting: 'Connessione a %1…',
          ready: 'Connesso a Lumen su %1', choose: 'In Lumen apri «Trasmetti» e scegli «%1».',
          name: 'Nome di questo dispositivo', disconnect: 'Disconnetti', lost: 'La connessione a Lumen è stata persa.',
          failed: 'Il flusso non può essere riprodotto su questo dispositivo.', sound: 'Premi OK o fai clic per l’audio' },
    pt: { searching: 'A procurar o Lumen nesta rede…', notFound: 'O Lumen não foi encontrado.',
          enter: 'Abra «Transmitir» no Lumen no computador e introduza o endereço apresentado.',
          connect: 'Ligar', search: 'Procurar novamente', connecting: 'A ligar a %1…',
          ready: 'Ligado ao Lumen em %1', choose: 'No Lumen, abra «Transmitir» e escolha «%1».',
          name: 'Nome deste dispositivo', disconnect: 'Desligar', lost: 'A ligação ao Lumen foi perdida.',
          failed: 'A transmissão não pode ser reproduzida neste dispositivo.', sound: 'Prima OK ou clique para ouvir o som' },
    nl: { searching: 'Lumen wordt gezocht in dit netwerk…', notFound: 'Lumen is niet gevonden.',
          enter: 'Open ‘Casten’ in Lumen op je computer en voer het getoonde adres in.',
          connect: 'Verbinden', search: 'Opnieuw zoeken', connecting: 'Verbinden met %1…',
          ready: 'Verbonden met Lumen op %1', choose: 'Open ‘Casten’ in Lumen en kies ‘%1’.',
          name: 'Naam van dit apparaat', disconnect: 'Verbinding verbreken', lost: 'De verbinding met Lumen is verbroken.',
          failed: 'De stream kan niet op dit apparaat worden afgespeeld.', sound: 'Druk op OK of klik voor geluid' },
    pl: { searching: 'Szukanie programu Lumen w tej sieci…', notFound: 'Nie znaleziono programu Lumen.',
          enter: 'Otwórz „Przesyłaj” w programie Lumen na komputerze i wpisz pokazany tam adres.',
          connect: 'Połącz', search: 'Szukaj ponownie', connecting: 'Łączenie z %1…',
          ready: 'Połączono z Lumen na %1', choose: 'W programie Lumen otwórz „Przesyłaj” i wybierz „%1”.',
          name: 'Nazwa tego urządzenia', disconnect: 'Rozłącz', lost: 'Utracono połączenie z programem Lumen.',
          failed: 'Tego strumienia nie można odtworzyć na tym urządzeniu.', sound: 'Naciśnij OK lub kliknij, aby włączyć dźwięk' },
    sv: { searching: 'Söker efter Lumen i det här nätverket…', notFound: 'Lumen hittades inte.',
          enter: 'Öppna ”Casta” i Lumen på datorn och ange adressen som visas där.',
          connect: 'Anslut', search: 'Sök igen', connecting: 'Ansluter till %1…',
          ready: 'Ansluten till Lumen på %1', choose: 'Öppna ”Casta” i Lumen och välj ”%1”.',
          name: 'Namn på den här enheten', disconnect: 'Koppla från', lost: 'Anslutningen till Lumen bröts.',
          failed: 'Strömmen kan inte spelas upp på den här enheten.', sound: 'Tryck på OK eller klicka för ljud' },
    cs: { searching: 'Hledání aplikace Lumen v této síti…', notFound: 'Aplikace Lumen nebyla nalezena.',
          enter: 'V aplikaci Lumen na počítači otevřete „Přenos“ a zadejte zobrazenou adresu.',
          connect: 'Připojit', search: 'Hledat znovu', connecting: 'Připojování k %1…',
          ready: 'Připojeno k aplikaci Lumen na %1', choose: 'V aplikaci Lumen otevřete „Přenos“ a zvolte „%1“.',
          name: 'Název tohoto zařízení', disconnect: 'Odpojit', lost: 'Spojení s aplikací Lumen bylo ztraceno.',
          failed: 'Tento přenos nelze na tomto zařízení přehrát.', sound: 'Stiskněte OK nebo klikněte pro zvuk' },
    tr: { searching: 'Bu ağda Lumen aranıyor…', notFound: 'Lumen bulunamadı.',
          enter: 'Bilgisayarınızdaki Lumen’de “Yayınla”yı açın ve orada gösterilen adresi girin.',
          connect: 'Bağlan', search: 'Yeniden ara', connecting: '%1 adresine bağlanılıyor…',
          ready: '%1 üzerindeki Lumen’e bağlanıldı', choose: 'Lumen’de “Yayınla”yı açın ve “%1” öğesini seçin.',
          name: 'Bu cihazın adı', disconnect: 'Bağlantıyı kes', lost: 'Lumen ile bağlantı kesildi.',
          failed: 'Yayın bu cihazda oynatılamıyor.', sound: 'Ses için OK’e basın veya tıklayın' },
    uk: { searching: 'Пошук Lumen у цій мережі…', notFound: 'Lumen не знайдено.',
          enter: 'Відкрийте «Трансляція» в Lumen на комп’ютері та введіть показану там адресу.',
          connect: 'Підключити', search: 'Шукати знову', connecting: 'Підключення до %1…',
          ready: 'Підключено до Lumen на %1', choose: 'У Lumen відкрийте «Трансляція» та виберіть «%1».',
          name: 'Назва цього пристрою', disconnect: 'Відключити', lost: 'З’єднання з Lumen втрачено.',
          failed: 'Цей потік неможливо відтворити на цьому пристрої.', sound: 'Натисніть OK або клацніть, щоб увімкнути звук' },
    ru: { searching: 'Поиск Lumen в этой сети…', notFound: 'Lumen не найден.',
          enter: 'Откройте «Трансляция» в Lumen на компьютере и введите показанный там адрес.',
          connect: 'Подключить', search: 'Искать снова', connecting: 'Подключение к %1…',
          ready: 'Подключено к Lumen на %1', choose: 'В Lumen откройте «Трансляция» и выберите «%1».',
          name: 'Имя этого устройства', disconnect: 'Отключить', lost: 'Соединение с Lumen потеряно.',
          failed: 'Этот поток нельзя воспроизвести на этом устройстве.', sound: 'Нажмите OK или щёлкните, чтобы включить звук' },
    ja: { searching: 'このネットワークで Lumen を探しています…', notFound: 'Lumen が見つかりませんでした。',
          enter: 'パソコンの Lumen で「キャスト」を開き、表示されたアドレスを入力してください。',
          connect: '接続', search: '再検索', connecting: '%1 に接続しています…',
          ready: '%1 の Lumen に接続しました', choose: 'Lumen で「キャスト」を開き、「%1」を選んでください。',
          name: 'このデバイスの名前', disconnect: '切断', lost: 'Lumen との接続が切れました。',
          failed: 'このデバイスではストリームを再生できません。', sound: 'OK を押すかクリックすると音が出ます' },
    zh: { searching: '正在此网络中查找 Lumen…', notFound: '未找到 Lumen。',
          enter: '在电脑上的 Lumen 中打开“投放”，然后输入那里显示的地址。',
          connect: '连接', search: '重新查找', connecting: '正在连接 %1…',
          ready: '已连接到 %1 上的 Lumen', choose: '在 Lumen 中打开“投放”并选择“%1”。',
          name: '此设备的名称', disconnect: '断开连接', lost: '与 Lumen 的连接已断开。',
          failed: '此设备无法播放该流。', sound: '按 OK 或点击以开启声音' },
    ko: { searching: '이 네트워크에서 Lumen을 찾는 중…', notFound: 'Lumen을 찾지 못했습니다.',
          enter: '컴퓨터의 Lumen에서 “전송”을 열고 거기에 표시된 주소를 입력하세요.',
          connect: '연결', search: '다시 찾기', connecting: '%1에 연결하는 중…',
          ready: '%1의 Lumen에 연결됨', choose: 'Lumen에서 “전송”을 열고 “%1”을(를) 선택하세요.',
          name: '이 기기의 이름', disconnect: '연결 끊기', lost: 'Lumen과의 연결이 끊어졌습니다.',
          failed: '이 기기에서는 스트림을 재생할 수 없습니다.', sound: '소리를 켜려면 OK를 누르거나 클릭하세요' }
  };
  var lang = (navigator.language || 'en').slice(0, 2).toLowerCase();
  var T = STR[lang] || STR.en;
  function tr(key, arg) { return (T[key] || STR.en[key]).replace('%1', arg === undefined ? '' : arg); }

  function $(id) { return document.getElementById(id); }
  var video = $('video');
  var body = document.body;

  function store(key, value) {
    try {
      if (value === undefined) return window.localStorage.getItem(key);
      window.localStorage.setItem(key, value);
    } catch (e) { /* private mode, old TV */ }
    return null;
  }
  function uuid() {
    var s = '';
    for (var i = 0; i < 32; i++) s += Math.floor(Math.random() * 16).toString(16);
    return s;
  }

  var base = '';                 // "http://host:port" of Lumen ('' = same origin)
  var serverName = '';
  var clientId = store('lumen.id') || uuid();
  store('lumen.id', clientId);
  var deviceName = store('lumen.name') || P.deviceName || (P.name ? 'TV' : 'Browser');
  var state = 'idle';            // reported to Lumen: idle | buffering | playing | error
  var mode = '';                 // screen: connect | idle | playing
  var session = 0;               // changes on every (re)connect: stale callbacks stop themselves
  var hls = null;
  var current = null;            // play command being shown
  var retries = 0;
  var watchdog = null;
  var lastTime = -1, stalled = 0;

  // ---- screens ----
  function show(newMode, status, hint) {
    mode = newMode;
    body.className = newMode;
    $('status').textContent = status || '';
    $('hint').textContent = hint || '';
    if (newMode !== 'playing') focusFirst();
  }
  function toast(text, ms) {
    var el = $('toast');
    el.textContent = text;
    el.style.display = text ? 'block' : 'none';
    if (text && ms) setTimeout(function () { if (el.textContent === text) el.style.display = 'none'; }, ms);
  }
  function focusables() {
    var ids = mode === 'connect' ? ['address', 'go', 'search'] : mode === 'idle' ? ['name', 'disconnect'] : [];
    var out = [];
    for (var i = 0; i < ids.length; i++) {
      var el = $(ids[i]);
      if (el.offsetParent !== null) out.push(el);
    }
    return out;
  }
  function focusFirst() {
    var f = focusables();
    // on a TV, do not open the on-screen keyboard right away
    var first = f[f.length - 1];
    if (first) first.focus();
  }
  function moveFocus(delta) {
    var f = focusables();
    if (!f.length) return;
    var i = f.indexOf(document.activeElement);
    f[(i + delta + f.length) % f.length].focus();
  }

  // ---- HTTP ----
  function request(method, url, timeout, done) {
    var xhr = new XMLHttpRequest();
    var finished = false;
    function finish(status, text) {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      var json = null;
      try { json = JSON.parse(text); } catch (e) { /* not JSON */ }
      done(status, json);
    }
    var timer = setTimeout(function () { try { xhr.abort(); } catch (e) { /* ignore */ } finish(0, ''); }, timeout);
    xhr.onreadystatechange = function () { if (xhr.readyState === 4) finish(xhr.status, xhr.responseText); };
    try {
      xhr.open(method, url, true);
      xhr.send(null);
    } catch (e) {
      finish(0, '');
    }
    return xhr;
  }
  function api(name, params) {
    var q = [];
    for (var k in params) if (params.hasOwnProperty(k)) q.push(k + '=' + encodeURIComponent(params[k]));
    return base + '/api/' + name + (q.length ? '?' + q.join('&') : '');
  }

  // ---- connection ----
  function normalize(address) {
    var a = String(address || '').replace(/^\s+|\s+$/g, '').replace(/\/+$/, '');
    if (!a) return '';
    if (!/^https?:\/\//.test(a)) a = 'http://' + a;
    if (!/:\d+$/.test(a)) a += ':' + PORT;
    return a;
  }

  function connectTo(address, onFail) {
    var my = ++session;
    stopPlayback();
    base = address;
    show('', tr('connecting', address.replace(/^https?:\/\//, '') || location.host), '');
    request('GET', api('info', {}), 3500, function (status, info) {
      if (my !== session) return;
      if (status !== 200 || !info || info.name !== 'Lumen') {
        if (onFail) onFail(); else showConnect(tr('notFound'));
        return;
      }
      serverName = info.host || '';
      if (!embedded) store('lumen.address', address);
      hello(my);
    });
  }

  function hello(my) {
    request('POST', api('hello', { id: clientId, name: deviceName, platform: P.name || 'browser' }), 5000, function (status) {
      if (my !== session) return;
      if (status !== 200) { setTimeout(function () { if (my === session) hello(my); }, 2000); return; }
      if (mode !== 'playing') showIdle();
      poll(my, 0);
    });
  }

  function poll(my, failures) {
    request('GET', api('poll', { id: clientId, state: state }), 40000, function (status, cmd) {
      if (my !== session) return;
      if (status === 200 && cmd) {
        handle(cmd);
        poll(my, 0);
      } else if (status === 404) {
        hello(my);                       // Lumen was restarted and no longer knows this device
      } else if (failures >= 6 && !embedded) {
        stopPlayback();
        showConnect(tr('lost'));
      } else {
        if (failures === 3) { stopPlayback(); show('', tr('lost'), ''); }
        setTimeout(function () { if (my === session) poll(my, failures + 1); }, 1500);
      }
    });
  }

  function showIdle() {
    $('name').value = deviceName;
    show('idle', tr('ready', serverName || base.replace(/^https?:\/\//, '') || location.host), tr('choose', deviceName));
  }

  function showConnect(message) {
    session++;
    $('address').value = (store('lumen.address') || '').replace(/^https?:\/\//, '');
    show('connect', message || '', tr('enter'));
  }

  // ---- search: try every address of the local /24 networks ----
  function localPrefixes(done) {
    var finished = false;
    function finish(ip) {
      if (finished) return;
      finished = true;
      var m = /^(\d+\.\d+\.\d+)\.\d+$/.exec(ip || '');
      // unknown own address: the most common home networks
      done(m ? [m[1]] : ['192.168.0', '192.168.1', '192.168.178', '192.168.2', '10.0.0']);
    }
    if (P.localIp) {
      try { P.localIp(finish); } catch (e) { finish(''); }
      setTimeout(function () { finish(''); }, 2000);
      return;
    }
    var RTC = window.RTCPeerConnection || window.webkitRTCPeerConnection;
    if (!RTC) { finish(''); return; }
    try {
      var pc = new RTC({ iceServers: [] });
      pc.createDataChannel('x');
      pc.onicecandidate = function (e) {
        var m = e && e.candidate && /(\d+\.\d+\.\d+\.\d+)/.exec(e.candidate.candidate);
        if (m) finish(m[1]);
      };
      pc.createOffer(function (o) { pc.setLocalDescription(o, function () {}, function () {}); }, function () { finish(''); });
    } catch (e) { finish(''); }
    setTimeout(function () { finish(''); }, 1500);
  }

  function search() {
    var my = ++session;
    // the address can be typed while the search runs
    $('address').value = (store('lumen.address') || '').replace(/^https?:\/\//, '');
    show('connect', tr('searching'), tr('enter'));
    localPrefixes(function (prefixes) {
      if (my !== session) return;
      var hosts = [];
      for (var p = 0; p < prefixes.length; p++)
        for (var i = 1; i < 255; i++) hosts.push('http://' + prefixes[p] + '.' + i + ':' + PORT);
      var next = 0, open = 0, found = false;
      function pump() {
        if (my !== session || found) return;
        while (open < 40 && next < hosts.length) {
          (function (address) {
            open++;
            request('GET', address + '/api/info', 1000, function (status, info) {
              open--;
              if (my !== session || found) return;
              if (status === 200 && info && info.name === 'Lumen') {
                found = true;
                connectTo(address);
                return;
              }
              if (next >= hosts.length && open === 0) showConnect(tr('notFound'));
              else pump();
            });
          })(hosts[next++]);
        }
      }
      pump();
    });
  }

  // ---- playback ----
  function setState(s) { state = s; }

  function handle(cmd) {
    if (cmd.cmd === 'play') {
      current = cmd;
      retries = 0;
      startPlayback();
    } else if (cmd.cmd === 'stop') {
      stopPlayback();
      showIdle();
    }
  }

  function stopPlayback() {
    current = null;
    clearInterval(watchdog);
    watchdog = null;
    if (hls) { try { hls.destroy(); } catch (e) { /* ignore */ } hls = null; }
    try { video.pause(); video.removeAttribute('src'); video.load(); } catch (e) { /* ignore */ }
    toast('');
    setState('idle');
  }

  function loadScript(src, done) {
    var s = document.createElement('script');
    s.src = src;
    s.onload = function () { done(true); };
    s.onerror = function () { done(false); };
    document.head.appendChild(s);
  }

  function startPlayback() {
    var cmd = current;
    if (!cmd) return;
    if (hls) { try { hls.destroy(); } catch (e) { /* ignore */ } hls = null; }
    show('playing', '', '');
    setState('buffering');
    lastTime = -1;
    stalled = 0;
    clearInterval(watchdog);
    watchdog = setInterval(watch, 2000);

    var native = video.canPlayType('application/vnd.apple.mpegurl') || video.canPlayType('application/x-mpegURL');
    if (P.attach) {
      P.attach(video, cmd, playbackFailed);        // platform player (e.g. Samsung AVPlay)
    } else if (native) {
      video.src = cmd.hls;
      play();
    } else if (window.MediaSource || window.ManagedMediaSource) {
      var attach = function () {
        if (cmd !== current) return;
        hls = new window.Hls({ liveSyncDurationCount: 2, liveMaxLatencyDurationCount: 6, maxLiveSyncPlaybackRate: 1.15,
                               manifestLoadingMaxRetry: 6, levelLoadingMaxRetry: 6, fragLoadingMaxRetry: 6 });
        hls.on(window.Hls.Events.ERROR, function (event, data) {
          if (!data.fatal) return;
          if (data.type === window.Hls.ErrorTypes.MEDIA_ERROR && retries < 2) { retries++; hls.recoverMediaError(); }
          else playbackFailed();
        });
        hls.loadSource(cmd.hls);
        hls.attachMedia(video);
        play();
      };
      if (window.Hls) attach();
      else loadScript('hls.light.min.js', function (ok) { if (ok && window.Hls && window.Hls.isSupported()) attach(); else playbackFailed(); });
    } else {
      video.src = cmd.hls;
      play();
    }
  }

  function play() {
    var p;
    try { p = video.play(); } catch (e) { p = null; }
    if (p && p['catch']) {
      p['catch'](function () {
        // browsers refuse sound before the first click: start muted and say so
        video.muted = true;
        var q = video.play();
        if (q && q['catch']) q['catch'](function () {});
        toast(tr('sound'));
      });
    }
  }

  function playbackFailed() {
    if (!current) return;
    if (retries < 4) {
      retries++;
      setTimeout(function () { if (current) startPlayback(); }, 1500);
      return;
    }
    setState('error');
    show('idle', tr('failed'), '');
  }

  function watch() {
    if (!current || mode !== 'playing') return;
    var t = video.currentTime;
    if (t === lastTime) {
      stalled++;
      if (stalled === 2) setState('buffering');
      if (stalled >= 5) { stalled = 0; playbackFailed(); }   // 10 s without progress: load again
    } else {
      stalled = 0;
      retries = 0;
      setState('playing');
    }
    lastTime = t;
  }

  video.addEventListener('playing', function () { if (current) setState('playing'); });
  video.addEventListener('waiting', function () { if (current) setState('buffering'); });
  video.addEventListener('error', function () { if (current && !hls) playbackFailed(); });
  video.addEventListener('ended', function () { if (current) playbackFailed(); });

  function unmute() {
    if (video.muted && current) { video.muted = false; toast(''); }
  }

  // ---- remote control ----
  var KEYS = {
    37: 'left', 38: 'up', 39: 'right', 40: 'down', 13: 'enter',
    8: 'back', 27: 'back', 461: 'back', 10009: 'back',
    415: 'play', 19: 'pause', 10252: 'playpause', 179: 'playpause', 413: 'stop',
    412: 'rewind', 417: 'forward', 10232: 'prev', 10233: 'next', 176: 'next', 177: 'prev',
    457: 'menu', 18: 'menu', 10133: 'menu', 10135: 'menu', 32: 'playpause'
  };
  if (P.keys) for (var code in P.keys) if (P.keys.hasOwnProperty(code)) KEYS[code] = P.keys[code];

  function sendKey(key) {
    request('POST', api('key', { id: clientId, key: key }), 4000, function () {});
  }

  document.addEventListener('keydown', function (e) {
    var key = KEYS[e.keyCode];
    var typing = document.activeElement && document.activeElement.tagName === 'INPUT';
    if (mode === 'playing') {
      if (!key) return;
      unmute();
      sendKey(key);
      e.preventDefault();
      return;
    }
    if (key === 'back' && !(typing && e.keyCode === 8)) {
      if (P.exit && (mode === 'connect' || mode === 'idle')) P.exit();
      return;
    }
    if (key === 'enter' && typing) {
      if (document.activeElement.id === 'address') $('go').click();
      else document.activeElement.blur();
      e.preventDefault();
    } else if (key === 'down' || (key === 'right' && !typing)) {
      moveFocus(1);
      e.preventDefault();
    } else if (key === 'up' || (key === 'left' && !typing)) {
      moveFocus(-1);
      e.preventDefault();
    }
  });
  document.addEventListener('click', unmute);

  // ---- controls ----
  $('go').textContent = tr('connect');
  $('search').textContent = tr('search');
  $('disconnect').textContent = tr('disconnect');
  $('nameLabel').textContent = tr('name');
  $('go').onclick = function () {
    var a = normalize($('address').value);
    if (a) connectTo(a);
  };
  $('search').onclick = search;
  $('disconnect').onclick = function () {
    request('POST', api('bye', { id: clientId }), 2000, function () {});
    if (embedded) { session++; stopPlayback(); show('', tr('lost'), ''); }
    else { store('lumen.address', ''); showConnect(''); }
  };
  $('name').onchange = function () {
    var n = $('name').value.replace(/^\s+|\s+$/g, '').slice(0, 40);
    if (!n || n === deviceName) return;
    deviceName = n;
    store('lumen.name', n);
    request('POST', api('hello', { id: clientId, name: deviceName, platform: P.name || 'browser' }), 5000, function () {});
    $('hint').textContent = tr('choose', deviceName);
  };
  if (embedded) $('disconnect').style.display = 'none';

  window.addEventListener('beforeunload', function () {
    try { navigator.sendBeacon && navigator.sendBeacon(api('bye', { id: clientId })); } catch (e) { /* ignore */ }
  });

  // ---- start ----
  if (P.init) P.init();
  if (embedded) {
    connectTo('');
  } else {
    var saved = store('lumen.address');
    if (saved) connectTo(saved, search);
    else search();
  }
})();
