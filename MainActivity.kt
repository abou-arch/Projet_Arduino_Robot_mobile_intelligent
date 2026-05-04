// MainActivity.kt
package com.example.voiceassistant

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import fi.iki.elonen.NanoHTTPD
import org.json.JSONObject

class MainActivity : AppCompatActivity() {

    private lateinit var server: CommandServer

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Request permissions
        requestPermissions()

        // Start HTTP server
        server = CommandServer()
        try {
            server.start()
            Toast.makeText(this, "Server started on port 8080", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to start server", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        server.stop()
    }

    private fun requestPermissions() {
        val permissions = arrayOf(
            Manifest.permission.CALL_PHONE,
            Manifest.permission.SEND_SMS,
            Manifest.permission.INTERNET
        )
        ActivityCompat.requestPermissions(this, permissions, 1)
    }

    inner class CommandServer : NanoHTTPD(8080) {
        override fun serve(session: IHTTPSession): Response {
            if (session.method == Method.POST && session.uri == "/command") {
                val body = session.getBody()
                val json = JSONObject(String(body))
                val action = json.getString("action")

                runOnUiThread {
                    when (action) {
                        "call" -> {
                            val number = json.optString("number", "")
                            makeCall(number)
                        }
                        "send_message" -> {
                            val text = json.optString("text", "")
                            sendMessage(text)
                        }
                        "open_app" -> {
                            val app = json.optString("app", "")
                            openApp(app)
                        }
                        else -> {
                            Toast.makeText(this@MainActivity, "Unknown action", Toast.LENGTH_SHORT).show()
                        }
                    }
                }

                return newFixedLengthResponse("Command executed")
            }
            return newFixedLengthResponse(Response.Status.NOT_FOUND, MIME_PLAINTEXT, "Not Found")
        }
    }

    private fun makeCall(number: String) {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CALL_PHONE) == PackageManager.PERMISSION_GRANTED) {
            val intent = Intent(Intent.ACTION_CALL, Uri.parse("tel:$number"))
            startActivity(intent)
        } else {
            Toast.makeText(this, "Call permission not granted", Toast.LENGTH_SHORT).show()
        }
    }

    private fun sendMessage(text: String) {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.SEND_SMS) == PackageManager.PERMISSION_GRANTED) {
            val intent = Intent(Intent.ACTION_SENDTO, Uri.parse("smsto:"))
            intent.putExtra("sms_body", text)
            startActivity(intent)
        } else {
            Toast.makeText(this, "SMS permission not granted", Toast.LENGTH_SHORT).show()
        }
    }

    private fun openApp(appName: String) {
        val appMap = mapOf(
            "youtube" to "com.google.android.youtube",
            "whatsapp" to "com.whatsapp",
            "chrome" to "com.android.chrome"
        )

        val packageName = appMap[appName.lowercase()] ?: return
        val intent = packageManager.getLaunchIntentForPackage(packageName)
        if (intent != null) {
            startActivity(intent)
        } else {
            Toast.makeText(this, "App not found", Toast.LENGTH_SHORT).show()
        }
    }
}