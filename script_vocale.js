/* =========================================================================
 *  SCRIPT — Page de commande vocale du Robot Mobile Intelligent
 *  Rôle :
 *    1. Utiliser l'API Web Speech du navigateur pour transcrire la voix
 *       en texte (en français).
 *    2. Analyser la phrase reconnue pour en extraire action, durée, vitesse.
 *    3. Envoyer une requête HTTP à l'ESP32 : /cmd?action=...&duration=...&speed=...
 *    4. Lire la réponse à voix haute (synthèse vocale du navigateur).
 *  Auteur : Abou Camara
 * ========================================================================= */


/* -------- Références aux éléments de la page -------- */
const micBtn         = document.getElementById('micBtn');
const micHint        = document.getElementById('micHint');
const transcriptBox  = document.getElementById('transcriptBox');
const actionBox      = document.getElementById('actionBox');
const historyBox     = document.getElementById('historyBox');
const ipField        = document.getElementById('esp32Ip');
const speakBackCheck = document.getElementById('speakBack');
const statusInd      = document.getElementById('statusIndicator');
const statusText     = document.getElementById('statusText');


/* -------- Vérification du support navigateur -------- */
const SpeechRecognition =
  window.SpeechRecognition || window.webkitSpeechRecognition;

if (!SpeechRecognition) {
  micBtn.disabled = true;
  micHint.textContent = "Navigateur non compatible. Utilisez Chrome ou Edge.";
}


/* -------- Configuration de la reconnaissance vocale -------- */
let recognition = null;
let listening   = false;

if (SpeechRecognition) {
  recognition = new SpeechRecognition();
  recognition.lang            = 'fr-FR';
  recognition.interimResults  = true;
  recognition.continuous      = false;
  recognition.maxAlternatives = 1;
}


/* -------- Mots-clés français -> actions ESP32 -------- */
const ACTION_KEYWORDS = [
  { keys: ['avance', 'avancer', 'va devant', 'forward'],          action: 'FWD'   },
  { keys: ['recule', 'reculer', 'arrière', 'arriere',
           'va derrière', 'backward'],                            action: 'BWD'   },
  { keys: ['gauche', 'tourne à gauche', 'left'],                  action: 'LEFT'  },
  { keys: ['droite', 'tourne à droite', 'right'],                 action: 'RIGHT' },
  { keys: ['stop', 'arrête', 'arrete', 'halte', 'pause'],         action: 'STOP'  },
];


/* -------- Analyse de la phrase reconnue -------- */
function parsePhrase(text) {
  const t = text.toLowerCase().trim();

  // Action
  let action = null;
  for (const { keys, action: a } of ACTION_KEYWORDS) {
    if (keys.some(k => t.includes(k))) { action = a; break; }
  }

  // Durée
  let duration = 1000;
  const dMatch = t.match(/(\d+)\s*(seconde|secondes|s)/);
  if (dMatch) duration = parseInt(dMatch[1], 10) * 1000;

  // Vitesse
  let speed = 200;
  if (/lentement|doucement|lent/.test(t))  speed = 110;
  if (/rapidement|vite|rapide/.test(t))    speed = 250;
  const vMatch = t.match(/vitesse\s*(\d+)/);
  if (vMatch) speed = parseInt(vMatch[1], 10);

  // STOP : tout à zéro
  if (action === 'STOP') { duration = 0; speed = 0; }

  return { action, duration, speed };
}


/* -------- Affichage / historique -------- */
function setStatus(state, label) {
  statusInd.classList.remove('listening', 'online');
  if (state) statusInd.classList.add(state);
  statusText.textContent = label;
}

function logHistory(text, type = '') {
  const line = document.createElement('div');
  line.className = 'line ' + type;
  const time = new Date().toLocaleTimeString();
  line.textContent = `[${time}] ${text}`;
  historyBox.appendChild(line);
  historyBox.scrollTop = historyBox.scrollHeight;

  while (historyBox.children.length > 50) {
    historyBox.removeChild(historyBox.firstChild);
  }
}


/* -------- Synthèse vocale -------- */
function speak(text) {
  if (!speakBackCheck.checked) return;
  if (!('speechSynthesis' in window)) return;

  const utt = new SpeechSynthesisUtterance(text);
  utt.lang = 'fr-FR';
  utt.rate = 1.05;
  window.speechSynthesis.cancel();
  window.speechSynthesis.speak(utt);
}


/* -------- Envoi au robot -------- */
async function sendToRobot(action, duration, speed) {
  const ip  = ipField.value.trim();
  const url = `http://${ip}/cmd?action=${action}&duration=${duration}&speed=${speed}`;

  try {
    const res = await fetch(url, { cache: 'no-store' });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const txt = await res.text();
    logHistory('← ' + txt, 'ok');
    setStatus('online', 'Commande exécutée');
    speak('Commande ' + frenchAction(action));
  } catch (err) {
    logHistory('Erreur : ' + err.message, 'err');
    setStatus('', 'Hors ligne');
    speak("Je n'arrive pas à joindre le robot.");
  }
}

function frenchAction(action) {
  switch (action) {
    case 'FWD':   return 'avance';
    case 'BWD':   return 'recule';
    case 'LEFT':  return 'gauche';
    case 'RIGHT': return 'droite';
    case 'STOP':  return 'stop';
    default:      return 'inconnue';
  }
}


/* -------- Traitement d'une phrase finale -------- */
function processPhrase(text) {
  transcriptBox.classList.remove('partial');
  transcriptBox.textContent = text;
  logHistory('Vous : ' + text, 'user');

  const { action, duration, speed } = parsePhrase(text);

  if (!action) {
    actionBox.textContent = 'Aucune action reconnue.';
    speak("Je n'ai pas compris la commande.");
    return;
  }

  actionBox.textContent =
    `${action}  |  durée ${duration} ms  |  vitesse ${speed}`;
  sendToRobot(action, duration, speed);
}


/* -------- Evenements de la reconnaissance vocale -------- */
if (recognition) {

  recognition.addEventListener('start', () => {
    listening = true;
    micBtn.classList.add('listening');
    setStatus('listening', "À l'écoute…");
    micHint.textContent = 'Parlez maintenant';
    transcriptBox.classList.add('partial');
    transcriptBox.textContent = '…';
  });

  recognition.addEventListener('end', () => {
    listening = false;
    micBtn.classList.remove('listening');
    if (statusText.textContent === "À l'écoute…") {
      setStatus('', 'Prêt à écouter');
    }
    micHint.textContent = 'Cliquez pour parler';
  });

  recognition.addEventListener('error', (e) => {
    setStatus('', 'Erreur micro : ' + e.error);
    logHistory('Erreur micro : ' + e.error, 'err');
  });

  recognition.addEventListener('result', (event) => {
    let interim = '';
    let final   = '';

    for (let i = event.resultIndex; i < event.results.length; i++) {
      const r = event.results[i];
      if (r.isFinal) final += r[0].transcript;
      else           interim += r[0].transcript;
    }

    if (interim) {
      transcriptBox.classList.add('partial');
      transcriptBox.textContent = interim;
    }
    if (final) processPhrase(final.trim());
  });
}


/* -------- Clic sur le bouton micro -------- */
micBtn.addEventListener('click', () => {
  if (!recognition) return;
  if (listening) {
    recognition.stop();
  } else {
    try {
      recognition.start();
    } catch (e) {
      logHistory('Micro déjà actif.', 'err');
    }
  }
});


/* -------- Exemples cliquables -------- */
document.querySelectorAll('.chip').forEach(chip => {
  chip.addEventListener('click', () => {
    processPhrase(chip.dataset.example);
  });
});


/* -------- Auto-remplissage IP si la page est servie par l'ESP32 -------- */
if (location.host && !location.host.startsWith('localhost') &&
    !location.protocol.startsWith('file')) {
  ipField.value = location.host;
}