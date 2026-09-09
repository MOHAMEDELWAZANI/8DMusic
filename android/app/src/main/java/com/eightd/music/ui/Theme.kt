package com.eightd.music.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

/**
 * The two inks from the mobile mockup: PAPER and INK.
 *
 * The desktop build keeps its palette in cpp/src/ui/Theme.h and derives both
 * themes from a handful of ground colours; this mirrors that idea with the
 * values the mockup actually uses, so the phone and the desktop read as the
 * same product rather than two takes on it.
 */
data class Palette(
    val dark: Boolean,
    val ground: Color,
    val text: Color,
    val dim: Color,
    val faint: Color,
    val ghost: Color,
    val accent: Color,
    val deep: Color,
    val line: Color,
    val lineSoft: Color,
    val presetBorder: Color,
    val presetTint: Color,
    val boxBg: Color,
    val boxBorder: Color,
    val mark: Color,
    val engineBg: Color,
    val engineText: Color,
    val engineOnBg: Color,
    val engineOnText: Color,
    val motion: Color,
    val dotOn: Color,
    val dotOff: Color,
    val meterOn: Color,
    val meterOff: Color,
    val orbitOn: Color,
    val orbitOff: Color,
    val statusOn: Color,
    val statusOff: Color,
    val panel: Color,
    val panelSoft: Color,
) {
    companion object {
        val Paper = Palette(
            dark = false,
            ground = Color(0xFFF3F2F2),
            text = Color(0xFF201E1D),
            dim = Color(0xFF605D5D),
            faint = Color(0xFF7D7979),
            ghost = Color(0xFF9B9797),
            accent = Color(0xFF0088B0),
            deep = Color(0xFF006786),
            line = Color(0xFFD7D3D3),
            lineSoft = Color(0xFFDFDBDB),
            presetBorder = Color(0xFFD7D3D3),
            presetTint = Color(0xFFE9F8FF),
            boxBg = Color(0xFFF8F4F4),
            boxBorder = Color(0xFFBAB6B6),
            mark = Color(0xFFF3F2F2),
            engineBg = Color(0xFF0088B0),
            engineText = Color(0xFFF8F4F4),
            engineOnBg = Color(0xFFEAE9E9),
            engineOnText = Color(0xFF006786),
            motion = Color(0xFFD6006C),
            dotOn = Color(0xFF605D5D),
            dotOff = Color(0xFF9B9797),
            meterOn = Color(0xFF0088B0),
            meterOff = Color(0xFFBAB6B6),
            orbitOn = Color(0xFF38A6CF),
            orbitOff = Color(0xFFC3DDEA),
            statusOn = Color(0xFF006786),
            statusOff = Color(0xFF7D7979),
            panel = Color(0xFFEAE9E9),
            panelSoft = Color(0xFFF8F4F4),
        )

        val Ink = Palette(
            dark = true,
            ground = Color(0xFF1A1918),
            text = Color(0xFFF3F2F2),
            dim = Color(0xFFBAB6B6),
            faint = Color(0xFF9B9797),
            ghost = Color(0xFF605D5D),
            accent = Color(0xFF62C5EE),
            deep = Color(0xFF99E0FF),
            line = Color(0xFF403D3C),
            lineSoft = Color(0xFF332F2F),
            presetBorder = Color(0xFF403D3C),
            presetTint = Color(0xFF0A303E),
            boxBg = Color(0xFF262423),
            boxBorder = Color(0xFF605D5D),
            mark = Color(0xFF0A303E),
            engineBg = Color(0xFF62C5EE),
            engineText = Color(0xFF0A303E),
            engineOnBg = Color(0xFF262423),
            engineOnText = Color(0xFF99E0FF),
            motion = Color(0xFFFF458E),
            dotOn = Color(0xFFE4E2E2),
            dotOff = Color(0xFF7D7979),
            meterOn = Color(0xFF62C5EE),
            meterOff = Color(0xFF605D5D),
            orbitOn = Color(0xFF62C5EE),
            orbitOff = Color(0xFF33474F),
            statusOn = Color(0xFF99E0FF),
            statusOff = Color(0xFF9B9797),
            panel = Color(0xFF262423),
            panelSoft = Color(0xFF221F1E),
        )
    }
}

val LocalPalette = staticCompositionLocalOf { Palette.Paper }

val palette: Palette
    @Composable get() = LocalPalette.current
