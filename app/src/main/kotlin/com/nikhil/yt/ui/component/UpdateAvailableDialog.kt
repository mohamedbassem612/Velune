package com.nikhil.yt.ui.component

import android.widget.Toast
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.nikhil.yt.utils.ApkInstaller
import com.nikhil.yt.utils.ReleaseInfo
import kotlinx.coroutines.launch

@Composable
fun UpdateAvailableDialog(
    release: ReleaseInfo,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var downloading by remember { mutableStateOf(false) }
    var progress by remember { mutableStateOf(0) }

    AlertDialog(
        onDismissRequest = { if (!downloading) onDismiss() },
        title = { Text("New version available") },
        text = {
            Column {
                Text("Version ${release.tagName.removePrefix("v")} is ready.")
                if (downloading) {
                    Spacer(Modifier.height(12.dp))
                    LinearProgressIndicator(
                        progress = { progress / 100f },
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Spacer(Modifier.height(4.dp))
                    Text("Downloading $progress%")
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = !downloading,
                onClick = {
                    val apkUrl = release.apkUrl
                    if (apkUrl == null) {
                        Toast.makeText(context, "No APK found in this release", Toast.LENGTH_LONG).show()
                        return@TextButton
                    }
                    if (!ApkInstaller.canInstall(context)) {
                        Toast.makeText(
                            context,
                            "Allow installs from Velune, then come back and press Update again",
                            Toast.LENGTH_LONG
                        ).show()
                        ApkInstaller.openInstallPermissionSettings(context)
                        return@TextButton
                    }
                    downloading = true
                    scope.launch {
                        try {
                            val file = ApkInstaller.download(context, apkUrl) { progress = it }
                            ApkInstaller.install(context, file)
                            onDismiss()
                        } catch (e: Exception) {
                            downloading = false
                            Toast.makeText(context, "Update failed: ${e.message}", Toast.LENGTH_LONG).show()
                        }
                    }
                },
            ) { Text("Update") }
        },
        dismissButton = {
            TextButton(enabled = !downloading, onClick = onDismiss) { Text("Later") }
        },
    )
}
