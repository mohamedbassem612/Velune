package com.nikhil.yt.ui.tv

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.focusGroup
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nikhil.yt.R

internal enum class TvPage(val label: Int, val icon: Int) {
    Home(R.string.home, R.drawable.home_outlined),
    Search(R.string.search, R.drawable.search),
    Library(R.string.filter_library, R.drawable.library_outlined),
    Settings(R.string.settings, R.drawable.settings),
}

@Composable
internal fun TvNavigationRail(
    page: TvPage,
    requesters: Map<TvPage, FocusRequester>,
    contentFocus: FocusRequester,
    onSelect: (TvPage) -> Unit,
    onRailFocus: (Boolean) -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxHeight().width(88.dp).onFocusChanged { onRailFocus(it.hasFocus) }.focusGroup(),
        color = Color.Black,
    ) {
        Column(
            modifier = Modifier.fillMaxSize().padding(horizontal = 10.dp, vertical = 22.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("V", color = TvAccent, fontSize = 30.sp, fontWeight = FontWeight.Black)
            Spacer(Modifier.height(30.dp))
            TvPage.entries.forEach { target ->
                var focused by remember { mutableStateOf(false) }
                val selected = page == target
                Surface(
                    onClick = { onSelect(target) },
                    modifier = Modifier.fillMaxWidth().height(68.dp)
                        .focusRequester(requesters.getValue(target))
                        .focusProperties { if (selected) right = contentFocus }
                        .onFocusChanged { focused = it.isFocused },
                    shape = RoundedCornerShape(14.dp),
                    color = when { focused -> Color.White; selected -> TvSurface; else -> Color.Transparent },
                    border = BorderStroke(2.dp, if (focused) TvAccent else Color.Transparent),
                ) {
                    Column(
                        modifier = Modifier.fillMaxSize(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center,
                    ) {
                        Icon(painterResource(target.icon), null, Modifier.size(23.dp), tint = if (focused) Color.Black else if (selected) Color.White else Color.White.copy(alpha = .5f))
                        Spacer(Modifier.height(5.dp))
                        Text(stringResource(target.label), fontSize = 10.sp, color = if (focused) Color.Black else if (selected) Color.White else Color.White.copy(alpha = .5f), maxLines = 1)
                    }
                }
                Spacer(Modifier.height(5.dp))
            }
            Spacer(Modifier.weight(1f))
            Text("VELUNE", color = Color.White.copy(alpha = .4f), fontSize = 9.sp, fontWeight = FontWeight.Bold)
        }
    }
}
