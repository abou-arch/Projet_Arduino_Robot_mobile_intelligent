import speech_recognition as sr
import requests

API_URL = "http://127.0.0.1:8000/process"

r = sr.Recognizer()

while True:
    with sr.Microphone() as source:
        print("🎤 Speak...")
        r.adjust_for_ambient_noise(source)
        audio = r.listen(source)

    try:
        text = r.recognize_google(audio, language="fr-FR")
        print("🧾 You said:", text)

        response = requests.post(API_URL, json={"text": text})

        print("🤖 Response:", response.json())

    except Exception as e:
        print("❌ Error:", e)