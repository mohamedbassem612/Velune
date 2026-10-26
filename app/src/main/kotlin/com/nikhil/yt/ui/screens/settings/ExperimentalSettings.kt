package com.nikhil.yt.ui.screens.settings

import android.text.format.Formatter
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nikhil.yt.LocalPlayerAwareWindowInsets
import com.nikhil.yt.R
import com.nikhil.yt.diagnostics.BandwidthLog
import com.nikhil.yt.utils.BandwidthDiagnostics
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExperimentalSettings(navController: NavController) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var snapshot by remember { mutableStateOf(BandwidthLog.snapshot()) }
    var uidTotals by remember { mutableStateOf(BandwidthDiagnostics.uidTotals()) }
    var filter by remember { mutableStateOf("") }
    var functions by remember { mutableStateOf(false) }
    var rollbackDialog by remember { mutableStateOf(false) }
    val exportSuccess = stringResource(R.string.bandwidth_export_success)
    val exportError = stringResource(R.string.bandwidth_export_error)
    val unknown = stringResource(R.string.bandwidth_unknown)
    fun bytes(value: Long) = if (value < 0) unknown else Formatter.formatShortFileSize(context, value)
    fun refresh() {
        snapshot = BandwidthLog.snapshot()
        uidTotals = BandwidthDiagnostics.uidTotals()
    }

    LaunchedEffect(Unit) {
        while (true) {
            refresh()
            delay(1000)
        }
    }
    val exporter = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/tab-separated-values")) { uri ->
        if (uri != null) {
            val captured = BandwidthLog.snapshot()
            val totals = BandwidthDiagnostics.uidTotals()
            scope.launch {
                val success = withContext(Dispatchers.IO) {
                    runCatching {
                        val content = "# appUidReceivedBytes=${totals.first}\tappUidSentBytes=${totals.second}\n" + BandwidthLog.export(captured)
                        requireNotNull(context.contentResolver.openOutputStream(uri)).bufferedWriter().use { it.write(content) }
                    }.isSuccess
                }
                Toast.makeText(context, if (success) exportSuccess else exportError, Toast.LENGTH_LONG).show()
            }
        }
    }

    val visible = remember(snapshot, filter) {
        snapshot.entries.asReversed().filter {
            filter.isBlank() || listOf(it.source, it.function, it.reason, it.endpoint, it.status).any { field ->
                field.contains(filter, ignoreCase = true)
            }
        }
    }
    val grouped = remember(visible) { visible.groupBy { it.source to it.function }.entries.sortedByDescending { group ->
        group.value.sumOf { it.received + it.sent }
    } }

    Scaffold(
        topBar = { TopAppBar(
            title = { Text(stringResource(R.string.experimental_settings)) },
            navigationIcon = { IconButton(onClick = { navController.popBackStack() }) {
                Icon(painterResource(R.drawable.arrow_back), stringResource(R.string.bandwidth_back))
            } },
        ) },
        contentWindowInsets = LocalPlayerAwareWindowInsets.current,
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                Text(stringResource(R.string.options), style = MaterialTheme.typography.titleLarge)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text(stringResource(R.string.bandwidth_enable), Modifier.weight(1f).padding(end = 12.dp))
                    Switch(checked = snapshot.enabled, onCheckedChange = {
                        BandwidthDiagnostics.setEnabled(context, it)
                        refresh()
                    })
                }
                Text(stringResource(R.string.bandwidth_explanation), style = MaterialTheme.typography.bodySmall)
            }
            item {
                Text(stringResource(R.string.bandwidth_payload_totals, bytes(snapshot.received), bytes(snapshot.sent)))
                Text(stringResource(R.string.bandwidth_uid_totals, bytes(uidTotals.first), bytes(uidTotals.second)))
                Text(stringResource(R.string.bandwidth_limits, BandwidthLog.MAX_ENTRIES, snapshot.dropped), style = MaterialTheme.typography.bodySmall)
            }
            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { exporter.launch("Velune-bandwidth-${System.currentTimeMillis()}.tsv") }) {
                        Text(stringResource(R.string.bandwidth_export))
                    }
                    OutlinedButton(onClick = { BandwidthDiagnostics.clear(); refresh() }) {
                        Text(stringResource(R.string.bandwidth_clear))
                    }
                }
                TextButton(onClick = { rollbackDialog = true }) { Text(stringResource(R.string.bandwidth_rollback)) }
            }
            item {
                OutlinedTextField(value = filter, onValueChange = { filter = it }, modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.bandwidth_filter)) }, singleLine = true)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(selected = !functions, onClick = { functions = false }, label = { Text(stringResource(R.string.bandwidth_logs)) })
                    FilterChip(selected = functions, onClick = { functions = true }, label = { Text(stringResource(R.string.bandwidth_functions)) })
                }
            }
            if (visible.isEmpty()) item { Text(stringResource(R.string.bandwidth_empty)) }
            if (functions) {
                items(grouped, key = { it.key.first + "|" + it.key.second }) { group ->
                    ElevatedCard(Modifier.fillMaxWidth()) {
                        SelectionContainer { Column(Modifier.padding(12.dp)) {
                            Text(group.key.first, style = MaterialTheme.typography.titleSmall)
                            Text(group.key.second, style = MaterialTheme.typography.bodySmall)
                            val measured = group.value.filterNot { it.method.startsWith("OBSERVED") }
                            Text(stringResource(R.string.bandwidth_function_totals, group.value.size,
                                if (measured.isEmpty()) unknown else bytes(measured.sumOf { it.received }),
                                if (measured.isEmpty()) unknown else bytes(measured.sumOf { it.sent })))
                            Text(group.value.first().reason, style = MaterialTheme.typography.bodySmall)
                        } }
                    }
                }
            } else {
                items(visible, key = { it.id }) { entry ->
                    ElevatedCard(Modifier.fillMaxWidth()) {
                        SelectionContainer { Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            Text(entry.source, style = MaterialTheme.typography.titleSmall)
                            Text("${DateFormat.getTimeInstance().format(Date(entry.timeMs))} · ${entry.method} · ${entry.status}", style = MaterialTheme.typography.bodySmall)
                            Text(entry.reason)
                            Text(entry.function, style = MaterialTheme.typography.bodySmall)
                            Text(entry.endpoint, style = MaterialTheme.typography.bodySmall)
                            val measured = !entry.method.startsWith("OBSERVED")
                            Text("↓ ${if (measured) bytes(entry.received) else unknown}   ↑ ${if (measured) bytes(entry.sent) else unknown}   ${entry.durationMs} ms")
                        } }
                    }
                }
            }
        }
    }
    if (rollbackDialog) AlertDialog(
        onDismissRequest = { rollbackDialog = false },
        title = { Text(stringResource(R.string.bandwidth_rollback)) },
        text = { Text(stringResource(R.string.bandwidth_rollback_explanation)) },
        confirmButton = { TextButton(onClick = {
            BandwidthDiagnostics.rollback(context)
            refresh()
            rollbackDialog = false
        }) { Text(stringResource(R.string.bandwidth_rollback)) } },
        dismissButton = { TextButton(onClick = { rollbackDialog = false }) { Text(stringResource(R.string.bandwidth_cancel)) } },
    )
}
