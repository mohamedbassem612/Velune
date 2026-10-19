/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.ui.utils

import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList

class ItemWrapper<T>(
    val item: T,
) {
    private val _isSelected = mutableStateOf(false)

    var isSelected: Boolean
        get() = _isSelected.value
        set(value) {
            _isSelected.value = value
        }
}


/**
 * Wraps items for multi-select and keeps the selection when the list refreshes.
 * Selection is matched by [key] (for example the song id), not by list position.
 */
@Composable
fun <T> rememberWrappedItems(
    items: List<T>?,
    key: (T) -> Any,
): SnapshotStateList<ItemWrapper<T>> {
    val previous = remember { arrayOfNulls<List<ItemWrapper<T>>>(1) }
    return remember(items) {
        val selectedKeys = previous[0]?.filter { it.isSelected }?.map { key(it.item) }?.toSet() ?: emptySet()
        val list = (items ?: emptyList()).map { item ->
            ItemWrapper(item).also { it.isSelected = key(item) in selectedKeys }
        }.toMutableStateList()
        previous[0] = list
        list
    }
}
