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
          failed: 'The stream cannot be played on this device.', sound: 'Press OK or click for sound',
          watch: 'What would you like to watch?',
          castCard: 'From Lumen',
          castOn: 'Connected to %1',
          castSearching: 'Searching…',
          castOff: 'Not connected',
          driveCard: 'USB drive',
          driveNone: 'None connected',
          settings: 'Settings',
          folderEmpty: 'Nothing here that can be played.',
          sounds: 'Interface sounds',
          music: 'Music in the menus',
          saver: 'Screen saver',
          address: 'Address of Lumen',
          help: 'The arrow keys choose, OK opens, Back goes one page back.' },
    de: { searching: 'Lumen wird in diesem Netz gesucht …', notFound: 'Lumen wurde nicht gefunden.',
          enter: 'In Lumen am Computer „Übertragen“ öffnen und die dort gezeigte Adresse eingeben.',
          connect: 'Verbinden', search: 'Erneut suchen', connecting: 'Verbindung zu %1 …',
          ready: 'Mit Lumen auf %1 verbunden', choose: 'In Lumen „Übertragen“ öffnen und „%1“ wählen.',
          name: 'Name dieses Geräts', disconnect: 'Trennen', lost: 'Die Verbindung zu Lumen ist abgebrochen.',
          failed: 'Der Strom lässt sich auf diesem Gerät nicht abspielen.', sound: 'OK drücken oder klicken für Ton',
          watch: 'Was möchtest du sehen?',
          castCard: 'Von Lumen',
          castOn: 'Verbunden mit %1',
          castSearching: 'Suche …',
          castOff: 'Nicht verbunden',
          driveCard: 'USB-Laufwerk',
          driveNone: 'Keines angeschlossen',
          settings: 'Einstellungen',
          folderEmpty: 'Hier liegt nichts, was sich abspielen lässt.',
          sounds: 'Töne der Oberfläche',
          music: 'Musik in den Menüs',
          saver: 'Bildschirmschoner',
          address: 'Adresse von Lumen',
          help: 'Pfeiltasten wählen, OK öffnet, Zurück geht eine Seite zurück.' },
    fr: { searching: 'Recherche de Lumen sur ce réseau…', notFound: 'Lumen est introuvable.',
          enter: 'Ouvrez « Diffuser » dans Lumen sur votre ordinateur et saisissez l’adresse affichée.',
          connect: 'Connecter', search: 'Rechercher à nouveau', connecting: 'Connexion à %1…',
          ready: 'Connecté à Lumen sur %1', choose: 'Dans Lumen, ouvrez « Diffuser » et choisissez « %1 ».',
          name: 'Nom de cet appareil', disconnect: 'Déconnecter', lost: 'La connexion à Lumen a été perdue.',
          failed: 'Le flux ne peut pas être lu sur cet appareil.', sound: 'Appuyez sur OK ou cliquez pour le son',
          watch: 'Que voulez-vous regarder ?',
          castCard: 'Depuis Lumen',
          castOn: 'Connecté à %1',
          castSearching: 'Recherche…',
          castOff: 'Non connecté',
          driveCard: 'Disque USB',
          driveNone: 'Aucun branché',
          settings: 'Réglages',
          folderEmpty: 'Rien de lisible ici.',
          sounds: 'Sons de l’interface',
          music: 'Musique dans les menus',
          saver: 'Économiseur d’écran',
          address: 'Adresse de Lumen',
          help: 'Les flèches choisissent, OK ouvre, Retour revient en arrière.' },
    es: { searching: 'Buscando Lumen en esta red…', notFound: 'No se encontró Lumen.',
          enter: 'Abre «Transmitir» en Lumen en tu ordenador e introduce la dirección que aparece.',
          connect: 'Conectar', search: 'Buscar de nuevo', connecting: 'Conectando con %1…',
          ready: 'Conectado a Lumen en %1', choose: 'En Lumen, abre «Transmitir» y elige «%1».',
          name: 'Nombre de este dispositivo', disconnect: 'Desconectar', lost: 'Se perdió la conexión con Lumen.',
          failed: 'La transmisión no se puede reproducir en este dispositivo.', sound: 'Pulsa OK o haz clic para el sonido',
          watch: '¿Qué quieres ver?',
          castCard: 'Desde Lumen',
          castOn: 'Conectado a %1',
          castSearching: 'Buscando…',
          castOff: 'Sin conexión',
          driveCard: 'Disco USB',
          driveNone: 'Ninguno conectado',
          settings: 'Ajustes',
          folderEmpty: 'Aquí no hay nada que reproducir.',
          sounds: 'Sonidos de la interfaz',
          music: 'Música en los menús',
          saver: 'Salvapantallas',
          address: 'Dirección de Lumen',
          help: 'Las flechas eligen, OK abre, Atrás vuelve una página.' },
    it: { searching: 'Ricerca di Lumen in questa rete…', notFound: 'Lumen non è stato trovato.',
          enter: 'Apri «Trasmetti» in Lumen sul computer e inserisci l’indirizzo mostrato.',
          connect: 'Connetti', search: 'Cerca di nuovo', connecting: 'Connessione a %1…',
          ready: 'Connesso a Lumen su %1', choose: 'In Lumen apri «Trasmetti» e scegli «%1».',
          name: 'Nome di questo dispositivo', disconnect: 'Disconnetti', lost: 'La connessione a Lumen è stata persa.',
          failed: 'Il flusso non può essere riprodotto su questo dispositivo.', sound: 'Premi OK o fai clic per l’audio',
          watch: 'Cosa vuoi guardare?',
          castCard: 'Da Lumen',
          castOn: 'Connesso a %1',
          castSearching: 'Ricerca…',
          castOff: 'Non connesso',
          driveCard: 'Disco USB',
          driveNone: 'Nessuno collegato',
          settings: 'Impostazioni',
          folderEmpty: 'Qui non c’è nulla da riprodurre.',
          sounds: 'Suoni dell’interfaccia',
          music: 'Musica nei menu',
          saver: 'Salvaschermo',
          address: 'Indirizzo di Lumen',
          help: 'Le frecce scelgono, OK apre, Indietro torna alla pagina precedente.' },
    pt: { searching: 'A procurar o Lumen nesta rede…', notFound: 'O Lumen não foi encontrado.',
          enter: 'Abra «Transmitir» no Lumen no computador e introduza o endereço apresentado.',
          connect: 'Ligar', search: 'Procurar novamente', connecting: 'A ligar a %1…',
          ready: 'Ligado ao Lumen em %1', choose: 'No Lumen, abra «Transmitir» e escolha «%1».',
          name: 'Nome deste dispositivo', disconnect: 'Desligar', lost: 'A ligação ao Lumen foi perdida.',
          failed: 'A transmissão não pode ser reproduzida neste dispositivo.', sound: 'Prima OK ou clique para ouvir o som',
          watch: 'O que quer ver?',
          castCard: 'Do Lumen',
          castOn: 'Ligado a %1',
          castSearching: 'A procurar…',
          castOff: 'Sem ligação',
          driveCard: 'Disco USB',
          driveNone: 'Nenhum ligado',
          settings: 'Definições',
          folderEmpty: 'Aqui não há nada para reproduzir.',
          sounds: 'Sons da interface',
          music: 'Música nos menus',
          saver: 'Proteção de ecrã',
          address: 'Endereço do Lumen',
          help: 'As setas escolhem, OK abre, Voltar regressa à página anterior.' },
    nl: { searching: 'Lumen wordt gezocht in dit netwerk…', notFound: 'Lumen is niet gevonden.',
          enter: 'Open ‘Casten’ in Lumen op je computer en voer het getoonde adres in.',
          connect: 'Verbinden', search: 'Opnieuw zoeken', connecting: 'Verbinden met %1…',
          ready: 'Verbonden met Lumen op %1', choose: 'Open ‘Casten’ in Lumen en kies ‘%1’.',
          name: 'Naam van dit apparaat', disconnect: 'Verbinding verbreken', lost: 'De verbinding met Lumen is verbroken.',
          failed: 'De stream kan niet op dit apparaat worden afgespeeld.', sound: 'Druk op OK of klik voor geluid',
          watch: 'Wat wil je kijken?',
          castCard: 'Van Lumen',
          castOn: 'Verbonden met %1',
          castSearching: 'Zoeken…',
          castOff: 'Niet verbonden',
          driveCard: 'USB-schijf',
          driveNone: 'Geen aangesloten',
          settings: 'Instellingen',
          folderEmpty: 'Hier staat niets wat je kunt afspelen.',
          sounds: 'Geluiden van de interface',
          music: 'Muziek in de menu’s',
          saver: 'Schermbeveiliging',
          address: 'Adres van Lumen',
          help: 'Pijltjes kiezen, OK opent, Terug gaat een pagina terug.' },
    pl: { searching: 'Szukanie programu Lumen w tej sieci…', notFound: 'Nie znaleziono programu Lumen.',
          enter: 'Otwórz „Przesyłaj” w programie Lumen na komputerze i wpisz pokazany tam adres.',
          connect: 'Połącz', search: 'Szukaj ponownie', connecting: 'Łączenie z %1…',
          ready: 'Połączono z Lumen na %1', choose: 'W programie Lumen otwórz „Przesyłaj” i wybierz „%1”.',
          name: 'Nazwa tego urządzenia', disconnect: 'Rozłącz', lost: 'Utracono połączenie z programem Lumen.',
          failed: 'Tego strumienia nie można odtworzyć na tym urządzeniu.', sound: 'Naciśnij OK lub kliknij, aby włączyć dźwięk',
          watch: 'Co chcesz obejrzeć?',
          castCard: 'Z Lumen',
          castOn: 'Połączono z %1',
          castSearching: 'Szukanie…',
          castOff: 'Brak połączenia',
          driveCard: 'Dysk USB',
          driveNone: 'Nic nie podłączono',
          settings: 'Ustawienia',
          folderEmpty: 'Nie ma tu nic do odtworzenia.',
          sounds: 'Dźwięki interfejsu',
          music: 'Muzyka w menu',
          saver: 'Wygaszacz ekranu',
          address: 'Adres Lumen',
          help: 'Strzałki wybierają, OK otwiera, Wstecz wraca o stronę.' },
    sv: { searching: 'Söker efter Lumen i det här nätverket…', notFound: 'Lumen hittades inte.',
          enter: 'Öppna ”Casta” i Lumen på datorn och ange adressen som visas där.',
          connect: 'Anslut', search: 'Sök igen', connecting: 'Ansluter till %1…',
          ready: 'Ansluten till Lumen på %1', choose: 'Öppna ”Casta” i Lumen och välj ”%1”.',
          name: 'Namn på den här enheten', disconnect: 'Koppla från', lost: 'Anslutningen till Lumen bröts.',
          failed: 'Strömmen kan inte spelas upp på den här enheten.', sound: 'Tryck på OK eller klicka för ljud',
          watch: 'Vad vill du titta på?',
          castCard: 'Från Lumen',
          castOn: 'Ansluten till %1',
          castSearching: 'Söker…',
          castOff: 'Inte ansluten',
          driveCard: 'USB-enhet',
          driveNone: 'Ingen ansluten',
          settings: 'Inställningar',
          folderEmpty: 'Här finns inget som går att spela.',
          sounds: 'Gränssnittets ljud',
          music: 'Musik i menyerna',
          saver: 'Skärmsläckare',
          address: 'Lumens adress',
          help: 'Piltangenterna väljer, OK öppnar, Tillbaka går en sida tillbaka.' },
    cs: { searching: 'Hledání aplikace Lumen v této síti…', notFound: 'Aplikace Lumen nebyla nalezena.',
          enter: 'V aplikaci Lumen na počítači otevřete „Přenos“ a zadejte zobrazenou adresu.',
          connect: 'Připojit', search: 'Hledat znovu', connecting: 'Připojování k %1…',
          ready: 'Připojeno k aplikaci Lumen na %1', choose: 'V aplikaci Lumen otevřete „Přenos“ a zvolte „%1“.',
          name: 'Název tohoto zařízení', disconnect: 'Odpojit', lost: 'Spojení s aplikací Lumen bylo ztraceno.',
          failed: 'Tento přenos nelze na tomto zařízení přehrát.', sound: 'Stiskněte OK nebo klikněte pro zvuk',
          watch: 'Na co se chcete dívat?',
          castCard: 'Z Lumenu',
          castOn: 'Připojeno k %1',
          castSearching: 'Hledání…',
          castOff: 'Nepřipojeno',
          driveCard: 'Disk USB',
          driveNone: 'Žádný nepřipojen',
          settings: 'Nastavení',
          folderEmpty: 'Není tu nic k přehrání.',
          sounds: 'Zvuky rozhraní',
          music: 'Hudba v nabídkách',
          saver: 'Spořič obrazovky',
          address: 'Adresa Lumenu',
          help: 'Šipky vybírají, OK otevírá, Zpět se vrací o stránku.' },
    tr: { searching: 'Bu ağda Lumen aranıyor…', notFound: 'Lumen bulunamadı.',
          enter: 'Bilgisayarınızdaki Lumen’de “Yayınla”yı açın ve orada gösterilen adresi girin.',
          connect: 'Bağlan', search: 'Yeniden ara', connecting: '%1 adresine bağlanılıyor…',
          ready: '%1 üzerindeki Lumen’e bağlanıldı', choose: 'Lumen’de “Yayınla”yı açın ve “%1” öğesini seçin.',
          name: 'Bu cihazın adı', disconnect: 'Bağlantıyı kes', lost: 'Lumen ile bağlantı kesildi.',
          failed: 'Yayın bu cihazda oynatılamıyor.', sound: 'Ses için OK’e basın veya tıklayın',
          watch: 'Ne izlemek istersiniz?',
          castCard: 'Lumen’den',
          castOn: '%1 bağlantısı kuruldu',
          castSearching: 'Aranıyor…',
          castOff: 'Bağlı değil',
          driveCard: 'USB sürücü',
          driveNone: 'Hiçbiri bağlı değil',
          settings: 'Ayarlar',
          folderEmpty: 'Burada oynatılacak bir şey yok.',
          sounds: 'Arayüz sesleri',
          music: 'Menülerde müzik',
          saver: 'Ekran koruyucu',
          address: 'Lumen adresi',
          help: 'Oklar seçer, OK açar, Geri bir sayfa geri gider.' },
    uk: { searching: 'Пошук Lumen у цій мережі…', notFound: 'Lumen не знайдено.',
          enter: 'Відкрийте «Трансляція» в Lumen на комп’ютері та введіть показану там адресу.',
          connect: 'Підключити', search: 'Шукати знову', connecting: 'Підключення до %1…',
          ready: 'Підключено до Lumen на %1', choose: 'У Lumen відкрийте «Трансляція» та виберіть «%1».',
          name: 'Назва цього пристрою', disconnect: 'Відключити', lost: 'З’єднання з Lumen втрачено.',
          failed: 'Цей потік неможливо відтворити на цьому пристрої.', sound: 'Натисніть OK або клацніть, щоб увімкнути звук',
          watch: 'Що хочете подивитися?',
          castCard: 'З Lumen',
          castOn: 'Підключено до %1',
          castSearching: 'Пошук…',
          castOff: 'Не підключено',
          driveCard: 'USB-диск',
          driveNone: 'Нічого не підключено',
          settings: 'Налаштування',
          folderEmpty: 'Тут немає нічого, що можна відтворити.',
          sounds: 'Звуки інтерфейсу',
          music: 'Музика в меню',
          saver: 'Заставка',
          address: 'Адреса Lumen',
          help: 'Стрілки обирають, OK відкриває, Назад повертає на сторінку.' },
    ru: { searching: 'Поиск Lumen в этой сети…', notFound: 'Lumen не найден.',
          enter: 'Откройте «Трансляция» в Lumen на компьютере и введите показанный там адрес.',
          connect: 'Подключить', search: 'Искать снова', connecting: 'Подключение к %1…',
          ready: 'Подключено к Lumen на %1', choose: 'В Lumen откройте «Трансляция» и выберите «%1».',
          name: 'Имя этого устройства', disconnect: 'Отключить', lost: 'Соединение с Lumen потеряно.',
          failed: 'Этот поток нельзя воспроизвести на этом устройстве.', sound: 'Нажмите OK или щёлкните, чтобы включить звук',
          watch: 'Что хотите посмотреть?',
          castCard: 'Из Lumen',
          castOn: 'Подключено к %1',
          castSearching: 'Поиск…',
          castOff: 'Нет подключения',
          driveCard: 'USB-диск',
          driveNone: 'Ничего не подключено',
          settings: 'Настройки',
          folderEmpty: 'Здесь нечего воспроизводить.',
          sounds: 'Звуки интерфейса',
          music: 'Музыка в меню',
          saver: 'Заставка',
          address: 'Адрес Lumen',
          help: 'Стрелки выбирают, OK открывает, Назад возвращает на страницу.' },
    ja: { searching: 'このネットワークで Lumen を探しています…', notFound: 'Lumen が見つかりませんでした。',
          enter: 'パソコンの Lumen で「キャスト」を開き、表示されたアドレスを入力してください。',
          connect: '接続', search: '再検索', connecting: '%1 に接続しています…',
          ready: '%1 の Lumen に接続しました', choose: 'Lumen で「キャスト」を開き、「%1」を選んでください。',
          name: 'このデバイスの名前', disconnect: '切断', lost: 'Lumen との接続が切れました。',
          failed: 'このデバイスではストリームを再生できません。', sound: 'OK を押すかクリックすると音が出ます',
          watch: '何を見ますか？',
          castCard: 'Lumen から',
          castOn: '%1 に接続済み',
          castSearching: '検索中…',
          castOff: '未接続',
          driveCard: 'USB ドライブ',
          driveNone: '接続なし',
          settings: '設定',
          folderEmpty: '再生できるものはありません。',
          sounds: 'インターフェースの効果音',
          music: 'メニューの音楽',
          saver: 'スクリーンセーバー',
          address: 'Lumen のアドレス',
          help: '矢印で選び、OK で開き、戻るで一つ前に戻ります。' },
    zh: { searching: '正在此网络中查找 Lumen…', notFound: '未找到 Lumen。',
          enter: '在电脑上的 Lumen 中打开“投放”，然后输入那里显示的地址。',
          connect: '连接', search: '重新查找', connecting: '正在连接 %1…',
          ready: '已连接到 %1 上的 Lumen', choose: '在 Lumen 中打开“投放”并选择“%1”。',
          name: '此设备的名称', disconnect: '断开连接', lost: '与 Lumen 的连接已断开。',
          failed: '此设备无法播放该流。', sound: '按 OK 或点击以开启声音',
          watch: '想看什么？',
          castCard: '来自 Lumen',
          castOn: '已连接到 %1',
          castSearching: '正在查找…',
          castOff: '未连接',
          driveCard: 'USB 驱动器',
          driveNone: '未连接',
          settings: '设置',
          folderEmpty: '这里没有可播放的内容。',
          sounds: '界面提示音',
          music: '菜单音乐',
          saver: '屏幕保护',
          address: 'Lumen 的地址',
          help: '方向键选择，OK 打开，返回回到上一页。' },
    ko: { searching: '이 네트워크에서 Lumen을 찾는 중…', notFound: 'Lumen을 찾지 못했습니다.',
          enter: '컴퓨터의 Lumen에서 “전송”을 열고 거기에 표시된 주소를 입력하세요.',
          connect: '연결', search: '다시 찾기', connecting: '%1에 연결하는 중…',
          ready: '%1의 Lumen에 연결됨', choose: 'Lumen에서 “전송”을 열고 “%1”을(를) 선택하세요.',
          name: '이 기기의 이름', disconnect: '연결 끊기', lost: 'Lumen과의 연결이 끊어졌습니다.',
          failed: '이 기기에서는 스트림을 재생할 수 없습니다.', sound: '소리를 켜려면 OK를 누르거나 클릭하세요',
          watch: '무엇을 보시겠어요?',
          castCard: 'Lumen에서',
          castOn: '%1에 연결됨',
          castSearching: '찾는 중…',
          castOff: '연결 안 됨',
          driveCard: 'USB 드라이브',
          driveNone: '연결된 것 없음',
          settings: '설정',
          folderEmpty: '재생할 수 있는 것이 없습니다.',
          sounds: '인터페이스 효과음',
          music: '메뉴 음악',
          saver: '화면 보호기',
          address: 'Lumen 주소',
          help: '방향키로 선택, OK로 열기, 뒤로로 이전 페이지.' }
  };
  var lang = (navigator.language || 'en').slice(0, 2).toLowerCase();
  var T = STR[lang] || STR.en;
  function tr(key, arg) { return (T[key] || STR.en[key]).replace('%1', arg === undefined ? '' : arg); }

  function $(id) { return document.getElementById(id); }
  var video = $('video');
  var body = document.body;
  var stage = $('stage');
  var over = $('over');

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
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function icon(name, cls) {
    var i = el('img', cls);
    i.src = 'ic-' + name + '.svg';
    i.alt = '';
    return i;
  }
  function setT(node, value) { node.style.webkitTransform = value; node.style.transform = value; }

  // The interface is laid out for a screen 1080 units high and scaled to the real one
  function fit() {
    var h = window.innerHeight || 1080, w = window.innerWidth || 1920, s = h / 1080;
    stage.style.width = Math.ceil(w / s) + 'px';
    over.style.width = Math.ceil(w / s) + 'px';
    setT(stage, 'scale(' + s + ')');
    setT(over, 'scale(' + s + ')');
    var box = $('saverBox');
    if (box && !saverOn) placeSaver();
  }

  // ---- settings of this device ----
  function flag(key, def) { var v = store(key); return v === null || v === undefined ? def : v === '1'; }
  var set = {
    sounds: flag('lumen.sounds', true),
    music: flag('lumen.music', true),
    saver: flag('lumen.saver', true)
  };
  function setFlag(name, on) { set[name] = on; store('lumen.' + name, on ? '1' : '0'); }

  var base = '';                 // "http://host:port" of Lumen ('' = same origin)
  var serverName = '';
  var clientId = store('lumen.id') || uuid();
  store('lumen.id', clientId);
  var deviceName = store('lumen.name') || P.deviceName || (P.name ? 'TV' : 'Browser');
  var state = 'idle';            // reported to Lumen: idle | buffering | playing | error
  var mode = 'ui';               // ui | playing
  var cast = 'searching';        // searching | connected | offline
  var castNote = '';             // a message about the last attempt
  var session = 0;               // changes on every (re)connect: stale callbacks stop themselves
  var hls = null;
  var current = null;            // play command being shown (cast)
  var local = null;              // file being played from a drive: {entry, siblings, index}
  var retries = 0;
  var watchdog = null;
  var lastTime = -1, stalled = 0;
  var introDone = false;

  // ---- sounds: short files, and a quiet loop in the menus (the same ones LumenOS plays) ----
  var Snd = (function () {
    var unlocked = false, effects = {}, music = null, level = 0, timer = null, wanted = false;
    var ok = false;
    try { var probe = document.createElement('audio'); ok = !!(probe.canPlayType && probe.canPlayType('audio/ogg; codecs="opus"')); } catch (e) { ok = false; }
    function make(name) {
      var a = new Audio();
      a.preload = 'auto';
      a.src = 'snd-' + name + '.ogg';
      return a;
    }
    function fade() {
      if (!music) return;
      var target = wanted ? 0.3 : 0;
      level += (target - level) * 0.18;
      if (Math.abs(target - level) < 0.004) { level = target; clearInterval(timer); timer = null; if (!wanted) { try { music.pause(); } catch (e) { /* ignore */ } } }
      try { music.volume = Math.max(0, Math.min(1, level)); } catch (e) { /* ignore */ }
    }
    return {
      unlock: function () {
        if (unlocked) return;
        unlocked = true;
        Snd.sync();
      },
      play: function (name) {
        if (!ok || !set.sounds) return;
        try {
          var a = effects[name] || (effects[name] = make(name));
          a.volume = 0.5;
          try { a.currentTime = 0; } catch (e) { /* not loaded yet */ }
          var p = a.play();
          if (p && p['catch']) p['catch'](function () {});
        } catch (e) { /* no sound on this device */ }
      },
      // music while the interface is shown and nothing plays
      sync: function () {
        if (!ok) return;
        wanted = set.music && mode === 'ui' && introDone && !saverOn && unlocked;
        if (wanted && !music) {
          music = make('music');
          music.loop = true;
          music.volume = 0;
        }
        if (wanted && music && music.paused) {
          var p; try { p = music.play(); } catch (e) { p = null; }
          if (p && p['catch']) p['catch'](function () {});
        }
        if (music && !timer) timer = setInterval(fade, 60);
      }
    };
  })();

  // ---- screens ----
  function toast(text, ms) {
    var e = $('toast');
    e.textContent = text;
    e.style.display = text ? 'block' : 'none';
    if (text && ms) setTimeout(function () { if (e.textContent === text) e.style.display = 'none'; }, ms);
  }
  function setMode(m) {
    mode = m;
    body.className = m === 'playing' ? 'playing' : 'ui';
    // (a native player behind the page: the page itself must be see-through while a film runs)
    if (P.native) document.documentElement.className = m === 'playing' ? 'native playing' : 'native';
    Snd.sync();
    if (m === 'playing') wakeSaver();
  }

  // pages: {kind: 'home' | 'list' | 'field', ...}; the stack is what Back walks down
  var stack = [];
  var view = $('view');
  var shown = null;              // {page, node, index}

  function render(page, direction) {
    var node = el('div', 'page ' + (direction ? 'in' : ''));
    var old = shown;
    var refs = page.kind === 'home' ? buildHome(page, node) : page.kind === 'field' ? buildField(page, node) : buildList(page, node);
    view.appendChild(node);
    shown = { page: page, node: node, refs: refs };
    $('help').textContent = page.kind === 'field' ? '' : tr('help');
    if (old) {
      old.node.className = 'page out';
      setTimeout(function () { if (old.node.parentNode) old.node.parentNode.removeChild(old.node); }, 230);
    }
    if (page.kind === 'field') { try { refs.input.focus(); } catch (e) { /* ignore */ } }
    else { try { document.activeElement && document.activeElement.blur(); } catch (e) { /* ignore */ } }
    if (page.shown) page.shown(page);
  }
  function push(page) {
    stack.push(page);
    Snd.play('select');
    render(page, true);
  }
  function pop() {
    if (stack.length <= 1) return;
    stack.pop();
    Snd.play('back');
    render(stack[stack.length - 1], true);
  }
  function refresh() {
    // content of the page on top changed (a status, the list): draw it again without a transition
    if (!stack.length || mode !== 'ui') return;
    var page = stack[stack.length - 1];
    if (page.rebuild) page.rebuild(page);
    if (page.kind === 'field') return;
    var old = shown;
    var node = el('div', 'page');
    var refs = page.kind === 'home' ? buildHome(page, node, true) : buildList(page, node, true);
    view.appendChild(node);
    shown = { page: page, node: node, refs: refs };
    if (old && old.node.parentNode) old.node.parentNode.removeChild(old.node);
  }

  // ---- home: cards ----
  function homeCards() {
    var list = [];
    var castSub = cast === 'connected' ? tr('castOn', serverName || base.replace(/^https?:\/\//, '') || location.host)
                : cast === 'searching' ? tr('castSearching') : tr('castOff');
    list.push({ title: tr('castCard'), sub: castSub, icon: 'screen', run: function () { push(castPage()); } });
    if (P.drives) list.push({ title: tr('driveCard'), sub: driveSub, icon: 'usb', run: function () { push(drivesPage()); } });
    list.push({ title: tr('settings'), sub: deviceName, icon: 'tune', run: function () { push(settingsPage()); } });
    return list;
  }
  var driveSub = '';
  var firstHome = true;
  function buildHome(page, node, quiet) {
    var cards = homeCards();
    if (page.index >= cards.length) page.index = cards.length - 1;
    var head = el('div', 'headline', tr('watch'));
    var row = el('div', 'cards');
    var nodes = [];
    for (var i = 0; i < cards.length; i++) {
      (function (i) {
        var c = el('div', 'card' + (i === page.index ? ' sel' : ''));
        c.style.left = (96 + i * 430) + 'px';
        var delay = quiet ? 0 : (firstHome ? 1850 : 60) + i * 70;
        if (quiet) { c.style.webkitAnimation = 'none'; c.style.animation = 'none'; c.style.opacity = '1'; }
        else { c.style.webkitAnimationDelay = delay + 'ms'; c.style.animationDelay = delay + 'ms'; }
        c.appendChild(el('div', 'halo'));
        var b = el('div', 'badge'); b.appendChild(icon(cards[i].icon)); c.appendChild(b);
        c.appendChild(el('div', 't', cards[i].title));
        c.appendChild(el('div', 's', cards[i].sub));
        c.onclick = function () { page.index = i; Snd.unlock(); selectCard(page, nodes, i, true); activateCard(page); };
        row.appendChild(c);
        nodes.push(c);
      })(i);
    }
    node.appendChild(head);
    node.appendChild(row);
    firstHome = false;
    return { cards: cards, nodes: nodes };
  }
  function selectCard(page, nodes, i, silent) {
    for (var k = 0; k < nodes.length; k++) nodes[k].className = 'card' + (k === i ? ' sel' : '');
    for (k = 0; k < nodes.length; k++) { nodes[k].style.webkitAnimation = 'none'; nodes[k].style.animation = 'none'; nodes[k].style.opacity = '1'; }
    if (!silent) Snd.play('move');
  }
  function activateCard(page) {
    var c = shown.refs.cards[page.index];
    if (c) c.run();
  }

  // ---- lists ----
  var ROW = 106;
  function buildList(page, node, quiet) {
    var left = el('div', 'left');
    if (page.icon) { var ico = el('div', 'ico'); ico.appendChild(icon(page.icon)); left.appendChild(ico); }
    if (page.kicker) left.appendChild(el('div', 'kicker', page.kicker));
    left.appendChild(el('div', 'title', page.title));
    var note = typeof page.note === 'function' ? page.note(page) : page.note;
    if (note) left.appendChild(el('div', 'note', note));
    if (page.busy) { var d = el('div', 'dots'); d.appendChild(el('span', 'dot')); d.appendChild(el('span', 'dot')); d.appendChild(el('span', 'dot')); left.appendChild(d); }
    var right = el('div', 'right');
    var inner = el('div', ''); inner.id = 'inner';
    var hl = el('div', ''); hl.id = 'hl';
    inner.appendChild(hl);
    var entries = typeof page.entries === 'function' ? page.entries(page) : page.entries;
    var rows = [];
    for (var i = 0; i < entries.length; i++) {
      (function (i) {
        var e = entries[i];
        var r = el('div', 'row' + (e.icon ? '' : ' noicon') + (e.detail ? ' hasd' : '') + (e.checked !== undefined ? ' sw' : '') + (e.off ? ' off' : ''));
        r.style.top = (i * ROW) + 'px';
        if (quiet) { r.style.webkitAnimation = 'none'; r.style.animation = 'none'; r.style.opacity = e.off ? '0.4' : '1'; }
        else { var dly = Math.min(i, 9) * 34; r.style.webkitAnimationDelay = dly + 'ms'; r.style.animationDelay = dly + 'ms'; }
        if (e.icon) r.appendChild(icon(e.icon, 'ri'));
        r.appendChild(el('div', 'l', e.label));
        if (e.detail) r.appendChild(el('div', 'd', e.detail));
        if (e.checked !== undefined) { var sw = el('div', 'sw' + (e.checked ? ' on' : '')); sw.appendChild(el('i')); r.appendChild(sw); }
        r.onclick = function () { Snd.unlock(); page.index = i; place(refs, page, true); activateRow(page); };
        inner.appendChild(r);
        rows.push(r);
      })(i);
    }
    right.appendChild(inner);
    node.appendChild(left);
    node.appendChild(right);
    var refs = { inner: inner, hl: hl, rows: rows, entries: entries, right: right };
    if (page.index >= entries.length) page.index = Math.max(0, entries.length - 1);
    place(refs, page, true);
    return refs;
  }
  var scrolls = 0;
  function place(refs, page, silent) {
    var n = refs.rows.length;
    if (!n) { refs.hl.style.display = 'none'; return; }
    refs.hl.style.display = 'block';
    setT(refs.hl, 'translateY(' + (page.index * ROW) + 'px)');
    for (var i = 0; i < n; i++) refs.rows[i].className = refs.rows[i].className.replace(/ ?\bsel\b/, '') + (i === page.index ? ' sel' : '');
    var viewH = 830, total = n * ROW;
    var want = page.index * ROW - viewH * 0.3;
    var scroll = Math.max(0, Math.min(want, Math.max(0, total - viewH + 24)));
    setT(refs.inner, 'translateY(' + (-scroll) + 'px)');
  }
  function activateRow(page) {
    var e = shown.refs.entries[page.index];
    if (!e || e.off || !e.run) return;
    if (e.checked !== undefined) Snd.play(e.checked ? 'off' : 'on');
    else if (!e.silent) Snd.play('select');
    e.run();
  }

  // ---- a field for text ----
  function buildField(page, node) {
    var left = el('div', 'left');
    if (page.icon) { var ico = el('div', 'ico'); ico.appendChild(icon(page.icon)); left.appendChild(ico); }
    left.appendChild(el('div', 'title', page.title));
    if (page.note) left.appendChild(el('div', 'note', page.note));
    var right = el('div', 'right');
    var input = el('input', 'field');
    input.type = 'text';
    input.value = page.value || '';
    input.setAttribute('autocomplete', 'off');
    input.setAttribute('spellcheck', 'false');
    if (page.placeholder) input.placeholder = page.placeholder;
    input.style.position = 'absolute'; input.style.left = '12px'; input.style.right = '12px'; input.style.top = '12px'; input.style.width = 'auto';
    right.appendChild(input);
    node.appendChild(left);
    node.appendChild(right);
    return { input: input };
  }

  // ---- the pages ----
  function homePage() { return { kind: 'home', index: 0 }; }

  function castPage() {
    return {
      kind: 'list', index: 0, icon: 'screen', title: tr('castCard'),
      busy: false,
      note: function () {
        if (cast === 'connected') return tr('ready', serverName || base.replace(/^https?:\/\//, '') || location.host) + '\n\n' + tr('choose', deviceName);
        if (cast === 'searching') return tr('searching');
        return (castNote ? castNote + '\n\n' : '') + tr('enter');
      },
      rebuild: function (p) { p.busy = cast === 'searching'; },
      entries: function () {
        var list = [];
        if (cast === 'connected') {
          list.push({ label: tr('name'), icon: 'tune', detail: deviceName, run: function () { push(namePage()); } });
          if (!embedded) list.push({ label: tr('disconnect'), icon: 'network', run: function () { disconnect(); } });
        } else {
          if (!embedded) {
            list.push({ label: tr('search'), icon: 'refresh', run: function () { search(); refresh(); } });
            list.push({ label: tr('address'), icon: 'network', detail: (store('lumen.address') || '').replace(/^https?:\/\//, ''), run: function () { push(addressPage()); } });
          } else {
            list.push({ label: tr('search'), icon: 'refresh', run: function () { connectTo(''); refresh(); } });
          }
        }
        return list;
      }
    };
  }
  function namePage() {
    return { kind: 'field', icon: 'tune', title: tr('name'), value: deviceName, accept: function (v) { renameDevice(v); } };
  }
  function addressPage() {
    return { kind: 'field', icon: 'network', title: tr('address'), note: tr('enter'), placeholder: '192.168.1.20:47800',
             value: (store('lumen.address') || '').replace(/^https?:\/\//, ''),
             accept: function (v) { var a = normalize(v); if (a) { cast = 'searching'; connectTo(a); } } };
  }
  function settingsPage() {
    return {
      kind: 'list', index: 0, icon: 'tune', title: tr('settings'),
      entries: function () {
        var list = [
          { label: tr('name'), icon: 'tune', detail: deviceName, run: function () { push(namePage()); } }
        ];
        if (!embedded) list.push({ label: tr('address'), icon: 'network', detail: (store('lumen.address') || '').replace(/^https?:\/\//, ''), run: function () { push(addressPage()); } });
        list.push({ label: tr('sounds'), icon: 'volume', checked: set.sounds, run: function () { setFlag('sounds', !set.sounds); refresh(); } });
        list.push({ label: tr('music'), icon: 'music', checked: set.music, run: function () { setFlag('music', !set.music); Snd.sync(); refresh(); } });
        list.push({ label: tr('saver'), icon: 'screen', checked: set.saver, run: function () { setFlag('saver', !set.saver); refresh(); } });
        return list;
      }
    };
  }

  // ---- drives (only where the platform can read them: P.drives) ----
  var MEDIA = /\.(mkv|mp4|m4v|mov|avi|ts|m2ts|mts|webm|wmv|mpg|mpeg|vob|flv|3gp|mp3|flac|m4a|aac|ogg|opus|wav|wma)$/i;
  function drivesPage() {
    var page = {
      kind: 'list', index: 0, icon: 'usb', title: tr('driveCard'), busy: true, drives: null,
      note: function (p) { return p.drives && !p.drives.length ? tr('driveNone') : ''; },
      entries: function (p) {
        var list = [];
        var d = p.drives || [];
        for (var i = 0; i < d.length; i++) {
          (function (drive) {
            list.push({ label: drive.name, icon: 'usb', detail: drive.detail || '', run: function () { push(folderPage(drive.path, drive.name, drive.name)); } });
          })(d[i]);
        }
        return list;
      },
      shown: function (p) {
        P.drives.list(function (drives) {
          p.drives = drives || [];
          p.busy = false;
          driveSub = p.drives.length ? (p.drives.length === 1 ? p.drives[0].name : p.drives.length + ' ×') : tr('driveNone');
          if (stack[stack.length - 1] === p) refresh();
        });
      }
    };
    return page;
  }
  function folderPage(path, title, kicker) {
    var page = {
      kind: 'list', index: 0, icon: 'folder', title: title, kicker: kicker, busy: true, items: null, path: path,
      note: function (p) { return p.items && !p.items.length ? tr('folderEmpty') : ''; },
      entries: function (p) {
        var list = [];
        var items = p.items || [];
        var files = [];
        for (var i = 0; i < items.length; i++) if (!items[i].dir) files.push(items[i]);
        for (i = 0; i < items.length; i++) {
          (function (it) {
            if (it.dir) list.push({ label: it.name, icon: 'folder', run: function () { push(folderPage(it.path, it.name, title)); } });
            else list.push({ label: it.name, icon: 'film', detail: sizeText(it.size), silent: true, run: function () { playLocal(it, files); } });
          })(items[i]);
        }
        return list;
      },
      shown: function (p) {
        P.drives.folder(p.path, function (items) {
          var out = [];
          items = items || [];
          for (var i = 0; i < items.length; i++) if (items[i].dir || MEDIA.test(items[i].name)) out.push(items[i]);
          out.sort(function (a, b) { return a.dir !== b.dir ? (a.dir ? -1 : 1) : (a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1); });
          p.items = out;
          p.busy = false;
          if (stack[stack.length - 1] === p) refresh();
        });
      }
    };
    return page;
  }
  function sizeText(n) {
    if (!(n > 0)) return '';
    var u = ['B', 'KB', 'MB', 'GB', 'TB'], i = 0;
    while (n >= 1000 && i < u.length - 1) { n /= 1000; i++; }
    return n.toFixed(n >= 100 || i < 2 ? 0 : 1) + ' ' + u[i];
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
  function setCast(s, note) {
    cast = s;
    castNote = note || '';
    $('netchip').textContent = s === 'connected' ? (serverName || base.replace(/^https?:\/\//, '') || location.host) : tr('castOff');
    refresh();
  }

  function connectTo(address, onFail) {
    var my = ++session;
    stopPlayback();
    base = address;
    setCast('searching', '');
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
      setCast('connected');
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
        if (failures === 3) { stopPlayback(); setCast('searching', ''); }
        setTimeout(function () { if (my === session) poll(my, failures + 1); }, 1500);
      }
    });
  }

  function showConnect(message) {
    session++;
    setCast('offline', message || '');
  }
  function disconnect() {
    request('POST', api('bye', { id: clientId }), 2000, function () {});
    if (embedded) { session++; stopPlayback(); setCast('offline', tr('lost')); }
    else { store('lumen.address', ''); showConnect(''); }
  }
  function renameDevice(v) {
    var n = (v || '').replace(/^\s+|\s+$/g, '').slice(0, 40);
    if (!n || n === deviceName) return;
    deviceName = n;
    store('lumen.name', n);
    if (cast === 'connected') request('POST', api('hello', { id: clientId, name: deviceName, platform: P.name || 'browser' }), 5000, function () {});
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
    setCast('searching', '');
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
      stopLocal(true);
      current = cmd;
      retries = 0;
      startPlayback();
    } else if (cmd.cmd === 'stop') {
      stopPlayback();
      setMode('ui');
      refresh();
    }
  }

  function stopPlayback() {
    current = null;
    stopLocal(false);
    if (P.detach) { try { P.detach(video); } catch (e) { /* ignore */ } }
    clearInterval(watchdog);
    watchdog = null;
    if (hls) { try { hls.destroy(); } catch (e) { /* ignore */ } hls = null; }
    try { video.pause(); video.removeAttribute('src'); video.load(); } catch (e) { /* ignore */ }
    toast('');
    setState('idle');
    if (mode === 'playing' && !local) setMode('ui');
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
    setMode('playing');
    hideOsd();
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
    if (local) { Snd.play('error'); var e = local.entry; stopLocal(false); setMode('ui'); toast(tr('failed'), 5000); refresh(); return; }
    if (!current) return;
    if (retries < 4) {
      retries++;
      setTimeout(function () { if (current) startPlayback(); }, 1500);
      return;
    }
    setState('error');
    current = null;
    setMode('ui');
    Snd.play('error');
    toast(tr('failed'), 6000);
    refresh();
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

  video.addEventListener('playing', function () { if (current) setState('playing'); updateOsd(); });
  video.addEventListener('pause', function () { if (local) { updateOsd(); showOsd(); } });
  video.addEventListener('waiting', function () { if (current) setState('buffering'); });
  video.addEventListener('timeupdate', function () { if (local) updateOsd(); });
  video.addEventListener('error', function () { if ((current && !hls) || local) playbackFailed(); });
  video.addEventListener('ended', function () {
    if (local) { nextLocal(1) || stopLocalToList(); return; }
    if (current) playbackFailed();
  });

  function unmute() {
    if (video.muted && (current || local)) { video.muted = false; toast(''); }
  }

  // ---- a file from a drive ----
  var osdTimer = null;
  function showOsd() {
    $('osd').className = 'on';
    clearTimeout(osdTimer);
    osdTimer = setTimeout(function () { if (!video.paused) hideOsd(); }, 4500);
  }
  function hideOsd() { clearTimeout(osdTimer); $('osd').className = ''; }
  function clock(t) {
    if (!(t >= 0) || t === Infinity) return '–:––';
    t = Math.floor(t);
    var h = Math.floor(t / 3600), m = Math.floor(t / 60) % 60, s = t % 60;
    return (h ? h + ':' + (m < 10 ? '0' : '') : '') + m + ':' + (s < 10 ? '0' : '') + s;
  }
  function updateOsd() {
    if (!local) return;
    var d = video.duration, t = video.currentTime || 0;
    $('osdFill').style.width = (d > 0 && d !== Infinity ? Math.min(100, t / d * 100) : 0) + '%';
    $('osdTime').textContent = clock(t) + ' / ' + clock(d);
    $('osdIcon').src = 'ic-' + (video.paused ? 'play' : 'volume') + '.svg';
  }
  function playLocal(entry, siblings) {
    var index = 0;
    for (var i = 0; i < siblings.length; i++) if (siblings[i].path === entry.path) index = i;
    startLocal({ entry: entry, siblings: siblings, index: index });
  }
  function startLocal(l) {
    stopPlayback();
    local = l;
    retries = 0;
    $('osdTitle').textContent = l.entry.name.replace(/\.[^.]+$/, '');
    setMode('playing');
    setState('idle');
    showOsd();
    updateOsd();
    try {
      if (P.drives.attach) P.drives.attach(video, l.entry, playbackFailed);
      else { video.src = P.drives.url(l.entry.path); play(); }
    } catch (e) { playbackFailed(); }
  }
  function stopLocal(silent) {
    if (!local) return;
    local = null;
    hideOsd();
    try { if (P.drives && P.drives.detach) P.drives.detach(video); } catch (e) { /* ignore */ }
    try { video.pause(); video.removeAttribute('src'); video.load(); } catch (e) { /* ignore */ }
  }
  function stopLocalToList() {
    stopLocal(false);
    setMode('ui');
    refresh();
  }
  function nextLocal(step) {
    if (!local) return false;
    var i = local.index + step;
    if (i < 0 || i >= local.siblings.length) return false;
    startLocal({ entry: local.siblings[i], siblings: local.siblings, index: i });
    return true;
  }
  var lastSeek = 0, seekRun = 0;
  function seekLocal(direction, big) {
    var now = new Date().getTime();
    seekRun = now - lastSeek < 700 ? seekRun + 1 : 0;
    lastSeek = now;
    var step = big ? 60 : seekRun > 6 ? 60 : seekRun > 2 ? 30 : 10;
    var d = video.duration, t = video.currentTime + direction * step;
    if (t < 0) t = 0;
    if (d > 0 && d !== Infinity && t > d - 1) t = d - 1;
    try { video.currentTime = t; } catch (e) { /* not seekable yet */ }
    showOsd();
    updateOsd();
  }
  function localKey(key) {
    showOsd();
    if (key === 'enter' || key === 'playpause') { if (video.paused) play(); else video.pause(); }
    else if (key === 'play') play();
    else if (key === 'pause') video.pause();
    else if (key === 'left') seekLocal(-1, false);
    else if (key === 'right') seekLocal(1, false);
    else if (key === 'rewind') seekLocal(-1, true);
    else if (key === 'forward') seekLocal(1, true);
    else if (key === 'next') nextLocal(1);
    else if (key === 'prev') { if (video.currentTime > 5) { try { video.currentTime = 0; } catch (e) { /* ignore */ } } else nextLocal(-1); }
    else if (key === 'back' || key === 'stop') stopLocalToList();
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

  function pageKey(key) {
    var page = stack[stack.length - 1];
    if (!page) return;
    if (page.kind === 'home') {
      var n = shown.refs.cards.length;
      if (key === 'left' && page.index > 0) { page.index--; selectCard(page, shown.refs.nodes, page.index); }
      else if (key === 'right' && page.index < n - 1) { page.index++; selectCard(page, shown.refs.nodes, page.index); }
      else if (key === 'enter') { Snd.play('select'); activateCard(page); }
      else if (key === 'back' && P.exit) P.exit();
    } else if (page.kind === 'list') {
      var m = shown.refs.rows.length;
      if (key === 'up' && page.index > 0) { page.index--; place(shown.refs, page); Snd.play('move'); }
      else if (key === 'down' && page.index < m - 1) { page.index++; place(shown.refs, page); Snd.play('move'); }
      else if (key === 'enter') activateRow(page);
      else if (key === 'back') pop();
      else if (key === 'menu') { stack = [stack[0]]; Snd.play('back'); render(stack[0], true); }
    }
  }

  document.addEventListener('keydown', function (e) {
    Snd.unlock();
    wakeSaver();
    var key = KEYS[e.keyCode];
    if (saverWoke) { saverWoke = false; e.preventDefault(); return; }
    if (mode === 'playing') {
      if (!key) return;
      unmute();
      if (local) localKey(key); else sendKey(key);
      e.preventDefault();
      return;
    }
    var page = stack[stack.length - 1];
    if (page && page.kind === 'field') {
      var input = shown.refs.input;
      if (key === 'enter') {
        var v = input.value;
        stack.pop();
        render(stack[stack.length - 1], true);
        Snd.play('select');
        if (page.accept) page.accept(v);
        e.preventDefault();
      } else if (key === 'back' && !(e.keyCode === 8 && input.value)) {
        pop();
        e.preventDefault();
      }
      return;
    }
    if (!key) return;
    pageKey(key);
    e.preventDefault();
  });
  document.addEventListener('click', function () { Snd.unlock(); unmute(); wakeSaver(); });
  document.addEventListener('mousemove', function () { wakeSaver(); });

  // ---- screen saver: after five minutes without input, a clock drifts over black ----
  var saverOn = false, saverWoke = false, saverTimer = null, saverMove = null;
  function placeSaver() {
    var box = $('saverBox');
    var w = Math.ceil((window.innerWidth || 1920) / ((window.innerHeight || 1080) / 1080));
    box.style.left = Math.round(80 + Math.random() * Math.max(0, w - 760)) + 'px';
    box.style.top = Math.round(120 + Math.random() * 640) + 'px';
  }
  function saverTick() {
    var d = new Date();
    $('saverClock').textContent = (d.getHours() < 10 ? '0' : '') + d.getHours() + ':' + (d.getMinutes() < 10 ? '0' : '') + d.getMinutes();
    try { $('saverDate').textContent = d.toLocaleDateString(lang, { weekday: 'long', day: 'numeric', month: 'long' }); } catch (e) { $('saverDate').textContent = ''; }
  }
  function startSaver() {
    if (saverOn || mode !== 'ui' || !set.saver || !introDone) return;
    saverOn = true;
    saverTick();
    $('saver').className = 'on';
    var box = $('saverBox');
    function cycle() {
      box.className = '';
      setTimeout(function () { if (!saverOn) return; placeSaver(); saverTick(); box.className = 'show'; }, 2600);
    }
    cycle();
    saverMove = setInterval(function () { if (saverOn) cycle(); }, 40000);
    Snd.sync();
  }
  function wakeSaver() {
    if (saverOn) {
      saverOn = false;
      saverWoke = true;
      clearInterval(saverMove);
      $('saver').className = '';
      $('saverBox').className = '';
      Snd.sync();
      setTimeout(function () { saverWoke = false; }, 400);   // the key that wakes does nothing else
    }
    clearTimeout(saverTimer);
    saverTimer = setTimeout(startSaver, 300000);
  }

  window.addEventListener('beforeunload', function () {
    try { navigator.sendBeacon && navigator.sendBeacon(api('bye', { id: clientId })); } catch (e) { /* ignore */ }
  });
  window.addEventListener('resize', fit);

  // ---- start ----
  function clockTick() {
    var d = new Date();
    $('clock').textContent = (d.getHours() < 10 ? '0' : '') + d.getHours() + ':' + (d.getMinutes() < 10 ? '0' : '') + d.getMinutes();
  }
  if (P.init) P.init();
  fit();
  clockTick();
  setInterval(clockTick, 10000);
  $('netchip').textContent = tr('castOff');
  $('help').textContent = tr('help');
  stack.push(homePage());
  render(stack[0], false);
  wakeSaver();
  Snd.play('start');
  setTimeout(function () {
    $('intro').className = 'gone';
    introDone = true;
    Snd.sync();
  }, 1750);
  if (location.search.indexOf('nointro') >= 0) { $('intro').style.display = 'none'; introDone = true; firstHome = false; }
  if (P.drives && P.drives.watch) P.drives.watch(function () { driveSub = ''; refresh(); });
  if (P.drives) P.drives.list(function (d) { d = d || []; driveSub = d.length ? (d.length === 1 ? d[0].name : d.length + ' ×') : tr('driveNone'); refresh(); });
  if (embedded) {
    connectTo('');
  } else {
    var saved = store('lumen.address');
    if (saved) connectTo(saved, search);
    else search();
  }
})();
