package com.nikhil.yt.playback

import android.content.Context
import androidx.media3.common.Player
import androidx.media3.session.CommandButton
import androidx.media3.session.DefaultMediaNotificationProvider
import androidx.media3.session.MediaSession
import com.google.common.collect.ImmutableList
import com.nikhil.yt.constants.MediaSessionConstants.ACTION_TOGGLE_LIKE
import com.nikhil.yt.constants.MediaSessionConstants.ACTION_TOGGLE_REPEAT_MODE

/** Keeps the expanded notification in repeat, previous, play/pause, next, favorite order. */
internal class VeluneMediaNotificationProvider(
    context: Context,
    notificationId: Int,
    channelId: String,
    channelName: Int,
) : DefaultMediaNotificationProvider(context, { notificationId }, channelId, channelName) {
    override fun getMediaButtons(
        session: MediaSession,
        playerCommands: Player.Commands,
        mediaButtonPreferences: ImmutableList<CommandButton>,
        showPauseButton: Boolean,
    ): ImmutableList<CommandButton> {
        // Older Android versions use notification action order. Newer System UI resolves the
        // session's media button preferences itself, with repeat before favorite in overflow slots.
        val buttons = super.getMediaButtons(session, playerCommands, mediaButtonPreferences, showPauseButton)
        return ImmutableList.copyOf(buttons.sortedBy { button ->
            when {
                button.sessionCommand?.customAction == ACTION_TOGGLE_REPEAT_MODE -> 0
                button.playerCommand == Player.COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM ||
                    button.playerCommand == Player.COMMAND_SEEK_TO_PREVIOUS -> 1
                button.playerCommand == Player.COMMAND_PLAY_PAUSE -> 2
                button.playerCommand == Player.COMMAND_SEEK_TO_NEXT_MEDIA_ITEM ||
                    button.playerCommand == Player.COMMAND_SEEK_TO_NEXT -> 3
                button.sessionCommand?.customAction == ACTION_TOGGLE_LIKE -> 4
                else -> 5
            }
        })
    }
}
