package com.nikhil.yt.ui.tv

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nikhil.yt.constants.HideExplicitKey
import com.nikhil.yt.constants.HideVideoKey
import com.nikhil.yt.innertube.YouTube
import com.nikhil.yt.innertube.models.SongItem
import com.nikhil.yt.innertube.models.filterExplicit
import com.nikhil.yt.innertube.models.filterVideo
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.models.toMediaMetadata
import com.nikhil.yt.utils.dataStore
import com.nikhil.yt.utils.get
import dagger.hilt.android.lifecycle.HiltViewModel
import dagger.hilt.android.qualifiers.ApplicationContext
import javax.inject.Inject
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class TvSearchState(
    val songs: List<MediaMetadata> = emptyList(),
    val loading: Boolean = false,
    val failed: Boolean = false,
    val submitted: Boolean = false,
)

@HiltViewModel
class TvSearchViewModel @Inject constructor(@ApplicationContext private val context: Context) : ViewModel() {
    private val mutableState = MutableStateFlow(TvSearchState())
    val state = mutableState.asStateFlow()
    private var searchJob: Job? = null

    fun search(query: String) {
        val term = query.trim()
        if (term.isEmpty()) return
        searchJob?.cancel()
        searchJob = viewModelScope.launch {
            mutableState.value = TvSearchState(loading = true, submitted = true)
            try {
                val songs = withContext(Dispatchers.IO) {
                    YouTube.search(term, YouTube.SearchFilter.FILTER_SONG).getOrThrow().items
                        .filterExplicit(context.dataStore.get(HideExplicitKey, false))
                        .filterVideo(context.dataStore.get(HideVideoKey, false))
                        .filterIsInstance<SongItem>()
                        .distinctBy { it.id }
                        .map { it.toMediaMetadata() }
                }
                ensureActive()
                mutableState.value = TvSearchState(songs = songs, submitted = true)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                mutableState.value = TvSearchState(failed = true, submitted = true)
            }
        }
    }
}
