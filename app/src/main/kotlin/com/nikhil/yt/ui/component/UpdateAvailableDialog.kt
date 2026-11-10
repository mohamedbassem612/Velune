package com.nikhil.yt.ui.component

import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import coil3.compose.AsyncImage
import com.nikhil.yt.R
import com.nikhil.yt.utils.ApkInstaller
import com.nikhil.yt.utils.ReleaseInfo
import com.nikhil.yt.utils.InvalidUpdatePackageException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

@Composable
fun UpdateAvailableDialog(
    release: ReleaseInfo,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var downloadJob by remember(release.tagName) { mutableStateOf<Job?>(null) }
    var progress by remember(release.tagName) { mutableIntStateOf(-1) }
    var error by remember(release.tagName) { mutableStateOf<String?>(null) }
    var readyFile by remember(release.apkUrl) {
        mutableStateOf(release.apkUrl?.let { ApkInstaller.downloaded(context, it) })
    }
    val downloading = downloadJob != null
    val version = release.tagName.removePrefix("v")
    // An optional Markdown image at the start of the release becomes the banner.
    val body = release.body.orEmpty().trim()
    val banner = remember(body) {
        Regex("""^!\[([^]]*)]\((https://[^\s)]+)\)""").find(body)
    }
    val notes = remember(body, banner) {
        if (banner == null) body else body.removeRange(banner.range).trim()
    }

    Dialog(
        onDismissRequest = { if (!downloading) onDismiss() },
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Surface(
            modifier = Modifier.padding(horizontal = 28.dp, vertical = 24.dp)
                .widthIn(max = 400.dp).fillMaxWidth(),
            shape = RoundedCornerShape(24.dp),
            color = Color(0xFF101012),
            contentColor = Color(0xFFF5F5F7),
            shadowElevation = 24.dp,
        ) {
            Column(modifier = Modifier.heightIn(max = 640.dp)) {
                Column(
                    modifier = Modifier.fillMaxWidth().background(
                        Brush.radialGradient(listOf(Color(0xFF303034), Color(0xFF101012)))
                    ).padding(horizontal = 24.dp, vertical = 22.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text(stringResource(R.string.update_dialog_title),
                        style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(6.dp))
                    Text(
                        stringResource(
                            if (downloading) R.string.update_dialog_downloading else R.string.update_dialog_available,
                            version,
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        textAlign = TextAlign.Center,
                    )
                    if (downloading) {
                        Spacer(Modifier.height(16.dp))
                        if (progress < 0) {
                            LinearProgressIndicator(modifier = Modifier.fillMaxWidth(),
                                color = Color.White, trackColor = Color(0xFF343438))
                        } else {
                            LinearProgressIndicator(progress = { progress / 100f },
                                modifier = Modifier.fillMaxWidth(), color = Color.White,
                                trackColor = Color(0xFF343438))
                            Spacer(Modifier.height(6.dp))
                            Text(stringResource(R.string.update_dialog_progress, progress),
                                style = MaterialTheme.typography.labelMedium)
                        }
                    }
                }
                HorizontalDivider(color = Color.White.copy(alpha = 0.08f))
                Column(
                    modifier = Modifier.weight(1f, fill = false)
                        .verticalScroll(rememberScrollState()).padding(24.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                ) {
                    Text(stringResource(R.string.update_dialog_whats_new),
                        style = MaterialTheme.typography.labelLarge, color = Color(0xFFAAAAAF))
                    if (banner != null) {
                        AsyncImage(model = banner.groupValues[2],
                            contentDescription = banner.groupValues[1].ifBlank { null },
                            contentScale = ContentScale.Crop,
                            modifier = Modifier.fillMaxWidth().aspectRatio(16f / 9f)
                                .clip(RoundedCornerShape(12.dp)))
                    } else {
                        Box(
                            modifier = Modifier.fillMaxWidth().height(116.dp)
                                .clip(RoundedCornerShape(12.dp)).background(
                                    Brush.linearGradient(listOf(Color(0xFF373044), Color(0xFF19191E)))
                                ),
                            contentAlignment = Alignment.Center,
                        ) {
                            Text("Velune", style = MaterialTheme.typography.headlineLarge,
                                fontWeight = FontWeight.Bold)
                        }
                    }
                    if (notes.isBlank()) {
                        Text(stringResource(R.string.update_dialog_no_notes),
                            style = MaterialTheme.typography.bodyMedium)
                    } else {
                        MarkdownText(markdown = notes, color = Color(0xFFF5F5F7))
                    }
                    error?.let {
                        Text(it, color = Color(0xFFFFB4AB), style = MaterialTheme.typography.bodySmall)
                    }
                }
                if (downloading) {
                    UpdateDialogAction(stringResource(R.string.cancel_button)) { downloadJob?.cancel() }
                } else {
                    UpdateDialogAction(stringResource(
                        when {
                            readyFile != null -> R.string.update_dialog_install
                            error != null -> R.string.retry
                            else -> R.string.update_dialog_download
                        }
                    )) {
                        val apkUrl = release.apkUrl
                        if (apkUrl == null) {
                            error = context.getString(R.string.update_dialog_no_apk)
                            return@UpdateDialogAction
                        }
                        if (readyFile != null && !ApkInstaller.canInstall(context)) {
                            Toast.makeText(context, R.string.update_dialog_install_permission, Toast.LENGTH_LONG).show()
                            ApkInstaller.openInstallPermissionSettings(context)
                            return@UpdateDialogAction
                        }
                        readyFile?.let { file ->
                            try {
                                ApkInstaller.install(context, file)
                                onDismiss()
                            } catch (e: Exception) {
                                if (e is InvalidUpdatePackageException) readyFile = null
                                error = context.getString(if (e is InvalidUpdatePackageException)
                                    R.string.update_dialog_wrong_package else R.string.update_dialog_failed)
                            }
                            return@UpdateDialogAction
                        }
                        error = null
                        progress = -1
                        downloadJob = scope.launch {
                            try {
                                val file = ApkInstaller.download(context, apkUrl) { progress = it }
                                readyFile = file
                                // Keep the completed package and let the user install when ready.
                            } catch (e: CancellationException) {
                                throw e
                            } catch (e: Exception) {
                                error = context.getString(if (e is InvalidUpdatePackageException)
                                    R.string.update_dialog_wrong_package else R.string.update_dialog_failed)
                            } finally {
                                downloadJob = null
                            }
                        }
                    }
                    UpdateDialogAction(stringResource(R.string.update_dialog_later), onDismiss)
                }
            }
        }
    }
}

@Composable
private fun UpdateDialogAction(label: String, onClick: () -> Unit) {
    HorizontalDivider(color = Color.White.copy(alpha = 0.06f))
    TextButton(onClick = onClick, shape = RoundedCornerShape(0.dp),
        modifier = Modifier.fillMaxWidth().heightIn(min = 56.dp),
        colors = ButtonDefaults.textButtonColors(contentColor = Color(0xFFF5F5F7))) {
        Text(label, style = MaterialTheme.typography.titleMedium)
    }
}
