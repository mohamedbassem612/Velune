package com.nikhil.yt.ui.screens.search

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nikhil.yt.LocalPlayerAwareWindowInsets
import com.nikhil.yt.R
import com.nikhil.yt.constants.SearchSource
import com.nikhil.yt.constants.SearchSourceKey
import com.nikhil.yt.utils.rememberEnumPreference
import java.net.URLEncoder

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MusicSearchScreen(navController: NavController) {
    val database = com.nikhil.yt.LocalDatabase.current
    val pauseHistory by com.nikhil.yt.utils.rememberPreference(com.nikhil.yt.constants.PauseSearchHistoryKey, false)
    var query by rememberSaveable(stateSaver = TextFieldValue.Saver) { mutableStateOf(TextFieldValue()) }
    var source by rememberEnumPreference(SearchSourceKey, SearchSource.ONLINE)
    val onSearch: (String) -> Unit = { text ->
        if (text.isNotBlank()) {
            navController.navigate("search/${URLEncoder.encode(text.trim(), "UTF-8")}")
            if (!pauseHistory) database.query { insert(com.nikhil.yt.db.entities.SearchHistory(query = text.trim())) }
        }
    }
    Column(Modifier.fillMaxSize().padding(LocalPlayerAwareWindowInsets.current.asPaddingValues())) {
        OutlinedTextField(
            value = query, onValueChange = { query = it },
            placeholder = { Text(stringResource(R.string.music_search_hint)) },
            leadingIcon = { Icon(painterResource(R.drawable.search), null) },
            trailingIcon = {
                if (query.text.isNotEmpty()) IconButton(onClick = { query = TextFieldValue() }) {
                    Icon(painterResource(R.drawable.close), stringResource(R.string.clear))
                }
            },
            singleLine = true,
            shape = MaterialTheme.shapes.large,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = { onSearch(query.text) }),
            colors = OutlinedTextFieldDefaults.colors(
                unfocusedBorderColor = androidx.compose.ui.graphics.Color.Transparent,
                focusedBorderColor = MaterialTheme.colorScheme.primary,
                unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainer,
                focusedContainerColor = MaterialTheme.colorScheme.surfaceContainer,
            ),
            modifier = Modifier.fillMaxWidth().padding(horizontal = 24.dp, vertical = 16.dp),
        )
        com.nikhil.yt.ui.component.ChipsRow(
            chips = listOf(SearchSource.ONLINE to stringResource(R.string.search_online),
                SearchSource.LOCAL to stringResource(R.string.music_my_library)),
            currentValue = source, onValueUpdate = { source = it },
        )
        Box(Modifier.weight(1f)) {
            when (source) {
                SearchSource.ONLINE -> OnlineSearchScreen(query.text, { query = it }, navController,
                    onSearch, {}, pureBlack = false)
                SearchSource.LOCAL -> LocalSearchScreen(query.text, navController, {}, pureBlack = false)
            }
        }
    }
}
