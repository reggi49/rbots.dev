package com.rbot.rbot_companion

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val CHANNEL = "com.rbot.rbot_companion/ble_permissions"
    private val PERMISSION_REQUEST_CODE = 1001
    private var pendingResult: MethodChannel.Result? = null

    private fun getRequiredPermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "checkPermissions" -> {
                    val status = checkPermissionsStatus()
                    result.success(status)
                }
                "requestPermissions" -> {
                    if (pendingResult != null) {
                        result.error("CONCURRENT_REQUEST", "A permission request is already in progress", null)
                        return@setMethodCallHandler
                    }
                    val missing = getMissingPermissions()
                    if (missing.isEmpty()) {
                        result.success(mapOf("status" to "granted", "missing" to emptyList<String>()))
                    } else {
                        pendingResult = result
                        ActivityCompat.requestPermissions(this, missing.toTypedArray(), PERMISSION_REQUEST_CODE)
                    }
                }
                "openAppSettings" -> {
                    try {
                        val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                            data = Uri.fromParts("package", packageName, null)
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        startActivity(intent)
                        result.success(true)
                    } catch (e: Exception) {
                        result.error("ACTIVITY_NOT_FOUND", e.localizedMessage, null)
                    }
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun getMissingPermissions(): List<String> {
        val required = getRequiredPermissions()
        return required.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
    }

    private fun checkPermissionsStatus(): Map<String, Any> {
        val missing = getMissingPermissions()
        if (missing.isEmpty()) {
            return mapOf(
                "status" to "granted",
                "missing" to emptyList<String>()
            )
        }

        // Check if any missing permission is permanently denied
        var isPermanentlyDenied = false
        val prefs = getSharedPreferences("ble_perms_prefs", MODE_PRIVATE)
        val hasRequestedBefore = prefs.getBoolean("has_requested_perms", false)

        if (hasRequestedBefore) {
            for (perm in missing) {
                if (!ActivityCompat.shouldShowRequestPermissionRationale(this, perm)) {
                    isPermanentlyDenied = true
                    break
                }
            }
        }

        val status = if (isPermanentlyDenied) "permanently_denied" else "denied"
        return mapOf(
            "status" to status,
            "missing" to missing
        )
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSION_REQUEST_CODE) {
            val prefs = getSharedPreferences("ble_perms_prefs", MODE_PRIVATE)
            prefs.edit().putBoolean("has_requested_perms", true).apply()

            val callback = pendingResult
            pendingResult = null

            val status = checkPermissionsStatus()
            callback?.success(status)
        }
    }
}
