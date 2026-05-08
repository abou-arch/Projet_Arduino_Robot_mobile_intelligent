/* =========================================================================
 *  SCRIPT — Page de contrôle du Robot Mobile Intelligent
 *  Rôle :
 *    1. Lire la vitesse, la durée et l'IP de l'ESP32 saisies par l'utilisateur.
 *    2. À chaque clic de bouton, envoyer une requête HTTP GET de la forme
 *       http://<IP>/cmd?action=...&duration=...&speed=...
 *    3. Afficher la réponse du robot dans la console.
 *    4. Vérifier toutes les 5 s que l'ESP32 répond (voyant en ligne / hors ligne).
 *    5. Permettre aussi le pilotage au clavier : ↑ ↓ ← → et la barre espace.
 *  Auteur : Abou Camara
 * ========================================================================= */


/* -------------------------------------------------------------------------
 *  REFERENCES AUX ELEMENTS DE LA PAGE
 *  On les récupère une seule fois au démarrage pour éviter de les chercher
 *  à chaque clic.
 * ------------------------------------------------------------------------- */
const speedSlider     = document.getElementById('speedSlider');
const durationSlider  = document.getElementById('durationSlider');
const speedValue      = document.getElementById('speedValue');
const durationValue   = document.getElementById('durationValue');
const ipField         = document.getElementById('esp32Ip');
const logBox          = document.getElementById('log');
const statusIndicator = document.getElementById('statusIndicator');
const statusText      = document.getElementById('statusText');


/* -------------------------------------------------------------------------
 *  MISE A JOUR DES ETIQUETTES DES CURSEURS
 *  Quand on bouge un curseur, on affiche immédiatement la nouvelle valeur.
 * ------------------------------------------------------------------------- */
speedSlider.addEventListener('input',
  () => speedValue.textContent = speedSlider.value);

durationSlider.addEventListener('input',
  () => durationValue.textContent = durationSlider.value);


/* -------------------------------------------------------------------------
 *  FONCTION : ajouter une ligne dans la console
 *  type peut valoir '', 'ok' ou 'err' pour la couleur.
 * ------------------------------------------------------------------------- */
function logLine(text, type = '') {
  const line = document.createElement('div');
  line.className = 'line ' + type;

  const time = new Date().toLocaleTimeString();
  line.textContent = `[${time}] ${text}`;

  logBox.appendChild(line);
  logBox.scrollTop = logBox.scrollHeight;

  // On évite que la console grossisse à l'infini : on garde 50 lignes max.
  while (logBox.children.length > 50) {
    logBox.removeChild(logBox.firstChild);
  }
}


/* -------------------------------------------------------------------------
 *  FONCTION : mettre à jour le voyant de connexion
 * ------------------------------------------------------------------------- */
function setOnline(online) {
  if (online) {
    statusIndicator.classList.add('online');
    statusText.textContent = 'Connecté';
  } else {
    statusIndicator.classList.remove('online');
    statusText.textContent = 'Hors ligne';
  }
}


/* -------------------------------------------------------------------------
 *  FONCTION : envoyer une commande au robot
 *  Construit l'URL et utilise fetch() pour faire l'appel HTTP.
 *  La fonction est "async" car fetch() renvoie une promesse.
 * ------------------------------------------------------------------------- */
async function sendCommand(action) {
  const ip       = ipField.value.trim();
  const speed    = speedSlider.value;
  const duration = action === 'STOP' ? 0 : durationSlider.value;

  const url = `http://${ip}/cmd?action=${action}&duration=${duration}&speed=${speed}`;
  logLine(`→ ${action} (durée ${duration} ms, vitesse ${speed})`);

  try {
    const res = await fetch(url, { method: 'GET', cache: 'no-store' });
    if (!res.ok) {
      throw new Error('HTTP ' + res.status);
    }
    const text = await res.text();
    logLine('← ' + text, 'ok');
    setOnline(true);
  } catch (err) {
    logLine('Erreur : ' + err.message, 'err');
    setOnline(false);
  }
}


/* -------------------------------------------------------------------------
 *  BRANCHEMENT DES BOUTONS
 *  Pour chaque bouton ayant la classe .btn, on lit sa valeur data-action
 *  et on appelle sendCommand().
 * ------------------------------------------------------------------------- */
document.querySelectorAll('.btn').forEach(btn => {
  btn.addEventListener('click',
    () => sendCommand(btn.dataset.action));
});


/* -------------------------------------------------------------------------
 *  PILOTAGE AU CLAVIER
 *  Flèches directionnelles + barre espace pour Stop.
 *  e.repeat évite les répétitions en rafale si on garde la touche enfoncée.
 * ------------------------------------------------------------------------- */
document.addEventListener('keydown', (e) => {
  if (e.repeat) return;
  switch (e.key) {
    case 'ArrowUp':    sendCommand('FWD');   break;
    case 'ArrowDown':  sendCommand('BWD');   break;
    case 'ArrowLeft':  sendCommand('LEFT');  break;
    case 'ArrowRight': sendCommand('RIGHT'); break;
    case ' ':          sendCommand('STOP');  break;
  }
});


/* -------------------------------------------------------------------------
 *  VERIFICATION PERIODIQUE DE LA CONNEXION
 *  Toutes les 5 secondes, on appelle /ping sur l'ESP32. S'il répond, le
 *  voyant passe en vert ; sinon, il reste en rouge.
 * ------------------------------------------------------------------------- */
setInterval(async () => {
  const ip = ipField.value.trim();
  if (!ip) return;
  try {
    const res = await fetch(`http://${ip}/ping`, { cache: 'no-store' });
    setOnline(res.ok);
  } catch {
    setOnline(false);
  }
}, 5000);