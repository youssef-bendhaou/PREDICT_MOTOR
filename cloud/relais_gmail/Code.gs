/**
 * Relais e-mail PredictMotor (Google Apps Script)
 *
 * Le service Render envoie une requete HTTPS a ce script,
 * qui envoie l'e-mail depuis TON compte Gmail.
 * (Render gratuit bloque le SMTP, d'ou ce relais.)
 *
 * 1. Remplace CLE et DESTINATAIRE ci-dessous.
 * 2. Deployer > Nouveau deploiement > Application Web
 *      Executer en tant que : Moi
 *      Qui a acces : Tout le monde
 * 3. Copie l'URL .../exec dans la variable EMAIL_RELAIS_URL de Render,
 *    et la meme CLE dans EMAIL_RELAIS_CLE.
 */
const CLE = "REMPLACE-MOI-PAR-UNE-CLE-SECRETE";
const DESTINATAIRE = "ton.adresse@gmail.com";

function doPost(e) {
  let d;
  try {
    d = JSON.parse(e.postData.contents);
  } catch (err) {
    return ContentService.createTextOutput("refuse : JSON invalide");
  }
  if (d.cle !== CLE) {
    return ContentService.createTextOutput("refuse : cle invalide");
  }
  MailApp.sendEmail(DESTINATAIRE, String(d.sujet || "PredictMotor"), String(d.texte || ""));
  return ContentService.createTextOutput("ok");
}

/** A lancer une fois a la main (bouton Executer) pour autoriser l'envoi d'e-mails. */
function testerEnvoi() {
  MailApp.sendEmail(DESTINATAIRE, "PredictMotor : test du relais", "Le relais Gmail fonctionne.");
}
