package com.example.kyaru

import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.widget.*
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.google.firebase.database.ktx.database
import com.google.firebase.ktx.Firebase

class MainActivity : AppCompatActivity() {

    private var isLightOn = true

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_main)

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        // 🔗 Firebase DB reference
        val database = Firebase.database
        val esp32Ref = database.getReference("ESP32")
        val brightnessRef = esp32Ref.child("brightness")
        val statusRef = esp32Ref.child("status")

        // 🔗 View references
        val slider = findViewById<SeekBar>(R.id.brightnessSlider)
        val input = findViewById<EditText>(R.id.brightnessInput)
        val toggleBtn = findViewById<Button>(R.id.toggleButton)
        val statusText = findViewById<TextView>(R.id.statusText)

        // 🌟 Cập nhật SeekBar khi nhập số
        input.addTextChangedListener(object : TextWatcher {
            override fun afterTextChanged(s: Editable?) {
                val value = s.toString().toIntOrNull()
                if (value != null && value in 0..100) {
                    slider.progress = value
                    brightnessRef.setValue(value)
                }
            }
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
        })

        // 🌟 Cập nhật ô nhập và Firebase khi kéo SeekBar
        slider.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                input.setText(progress.toString())
                brightnessRef.setValue(progress)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // 🌟 Nút ON/OFF
        toggleBtn.setOnClickListener {
            isLightOn = !isLightOn
            statusText.text = "Status: " + if (isLightOn) "ON" else "OFF"
            statusRef.setValue(if (isLightOn) "ON" else "OFF")
        }

        // 🌟 Nút kiểm tra Wi-Fi (giả lập)

    }
}
