import speech_recognition as sr
import requests
import pyttsx3

FASTAPI_URL = "http://127.0.0.1:8000/process"
SERVER_TIMEOUT = 120
ESP32_URL = "http://192.168.1.100/command"  # Change this to your ESP32 IP address
ANDROID_URL = "192.168.1.5"  # Change this to your Android app IP address
r = sr.Recognizer()
mic = sr.Microphone()

def speak(text):
    print("Robot:", text)
    try:
        engine = pyttsx3.init()
        engine.setProperty('rate', 170)
        engine.say(text)
        engine.runAndWait()
        engine.stop()
    except Exception as e:
        print("TTS ERROR:", type(e).__name__, e)
        try:
            alt_engine = pyttsx3.init()
            alt_engine.setProperty('rate', 170)
            alt_engine.say(text)
            alt_engine.runAndWait()
            alt_engine.stop()
        except Exception as e2:
            print("TTS RETRY ERROR:", type(e2).__name__, e2)


def is_goodbye(text):
    goodbye_words = ["au revoir", "bye", "adieu", "exit", "quitter", "stop", "au revoir", "ciao", "salut"]
    return any(word in text.lower() for word in goodbye_words)


with mic as source:
    print("Calibration...")
    r.adjust_for_ambient_noise(source, duration=1)

print("Robot ready!")

while True:
    try:
        with mic as source:
            print("\nSpeak...")

            audio = r.listen(source, timeout=5, phrase_time_limit=5)

        try:
            text = r.recognize_google(audio, language="fr-FR")
        except sr.UnknownValueError:
            speak("I didn't understand")
            continue

        print("You:", text)

        if is_goodbye(text):
            speak("Au revoir! À bientôt!")
            print("Conversation terminated.")
            break

        # 🔥 DEBUG (important)
        try:
            res = requests.post(
                FASTAPI_URL,
                json={"text": text},
                timeout=SERVER_TIMEOUT
            )

            print("HTTP STATUS:", res.status_code)
            print("RAW RESPONSE:", res.text)

            if res.status_code != 200:
                print("SERVER ERROR:", res.status_code, res.text)
                speak("Server error")
                continue

            data = res.json()

        except requests.exceptions.RequestException as e:
            print("REQUEST ERROR:", type(e).__name__, e)
            speak("Server not available")
            continue
        except ValueError as e:
            print("JSON ERROR:", type(e).__name__, e)
            speak("Invalid server response")
            continue

        # 🤖 response handling
        if data.get("type") == "chat":
            reply = data.get("reply", "")
            speak(reply)

        elif data.get("type") == "robot_command":
            cmd = data.get("command", {})
            action = cmd.get("action", "UNKNOWN")
            duration = cmd.get("duration")
            if duration:
                speak(f"Command executed {action} for {duration} seconds")
            else:
                speak("Command executed " + action)
            
            # Send command to ESP32
            try:
                esp_res = requests.post(ESP32_URL, json=cmd, timeout=10)
                print(f"ESP32 response: {esp_res.status_code} - {esp_res.text}")
            except Exception as e:
                print(f"ESP32 send error: {e}")
                speak("Failed to send command to robot")

        elif data.get("type") == "phone_command":
            cmd = data.get("command", {})
            action = cmd.get("action", "unknown")
            speak(f"Executing phone command: {action}")
            
            # Send command to Android
            try:
                android_res = requests.post(ANDROID_URL, json=cmd, timeout=10)
                print(f"Android response: {android_res.status_code} - {android_res.text}")
            except Exception as e:
                print(f"Android send error: {e}")
                speak("Failed to send command to phone")

    except Exception as e:
        print("ERROR:", e)
        speak("Error occurred")