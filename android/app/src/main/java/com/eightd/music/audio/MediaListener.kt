package com.eightd.music.audio

import android.service.notification.NotificationListenerService

/**
 * Exists only to be granted.
 *
 * Reading what another app is playing goes through MediaSessionManager, and
 * that requires notification-listener access — which is granted to a service,
 * not to an app. This one listens to nothing; its presence is the permission.
 */
class MediaListener : NotificationListenerService()
