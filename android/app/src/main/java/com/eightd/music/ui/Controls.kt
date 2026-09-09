package com.eightd.music.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

private val Square = RoundedCornerShape(2.dp)

/**
 * A labelled slider with its value and hint, matching the desktop rail: the
 * value is always visible, and a control that cannot do anything right now is
 * dimmed rather than hidden, so the layout never jumps.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ParamSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    display: String,
    hint: String? = null,
    enabled: Boolean = true,
    steps: Int = 0,
    onChange: (Float) -> Unit,
) {
    val p = palette
    Column(Modifier.fillMaxWidth().padding(top = 14.dp).alpha(if (enabled) 1f else 0.45f)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, 17.sp, p.text)
            Text(display, 17.sp, p.accent, FontWeight.SemiBold)
        }
        val on = if (enabled) p.accent else p.ghost
        Slider(
            value = value.coerceIn(range.start, range.endInclusive),
            onValueChange = onChange,
            valueRange = range,
            steps = steps,
            enabled = enabled,
            // Material 3 draws a tall bar thumb and breaks the track around it.
            // The mockup has a 3px rail and a round thumb, so supply both.
            thumb = {
                Box(Modifier.size(20.dp), contentAlignment = Alignment.Center) {
                    Box(Modifier.size(14.dp).background(on, CircleShape))
                }
            },
            track = { st ->
                val f = ((st.value - range.start) / (range.endInclusive - range.start))
                    .coerceIn(0f, 1f)
                Box(Modifier.fillMaxWidth().height(3.dp).background(p.line)) {
                    Box(Modifier.fillMaxWidth(f).height(3.dp).background(on))
                }
            },
            modifier = Modifier.fillMaxWidth().height(32.dp),
        )
        if (!hint.isNullOrEmpty()) {
            Text(hint, 13.sp, p.faint, modifier = Modifier.padding(top = 2.dp))
        }
    }
}

/** A select, drawn as the mockup's bordered field rather than a Material menu. */
@Composable
fun <T> Field(
    value: T,
    options: List<T>,
    label: (T) -> String,
    enabled: Boolean = true,
    modifier: Modifier = Modifier,
    onPick: (T) -> Unit,
) {
    val p = palette
    var open by remember { mutableStateOf(false) }

    Box(modifier.alpha(if (enabled) 1f else 0.45f)) {
        Row(
            Modifier
                .fillMaxWidth()
                .defaultMinSize(minHeight = 48.dp)
                .border(1.dp, p.boxBorder, Square)
                .background(p.boxBg, Square)
                .clickable(enabled = enabled) { open = true }
                .padding(horizontal = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(label(value), 16.sp, p.text, maxLines = 1, modifier = Modifier.weight(1f, false))
            Text("▼", 11.sp, p.faint)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { o ->
                DropdownMenuItem(
                    text = { Text(label(o), 16.sp, if (o == value) p.accent else p.text) },
                    onClick = { onPick(o); open = false },
                )
            }
        }
    }
}

/** A checkbox drawn the mockup's way: a 22px square that fills when checked. */
@Composable
fun CheckRow(label: String, checked: Boolean, enabled: Boolean = true, onToggle: () -> Unit) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(top = 16.dp)
            .alpha(if (enabled) 1f else 0.45f)
            .clickable(enabled = enabled) { onToggle() },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Box(
            Modifier
                .size(22.dp)
                .border(1.dp, if (checked) p.accent else p.boxBorder, Square)
                .background(if (checked) p.accent else p.boxBg, Square),
            contentAlignment = Alignment.Center,
        ) {
            if (checked) Text("✓", 13.sp, p.mark)
        }
        Text(label, 16.sp, p.text)
    }
}

@Composable
fun Chip(label: String, selected: Boolean, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .defaultMinSize(minHeight = 44.dp)
            .border(1.dp, if (selected) p.accent else p.presetBorder, Square)
            .background(if (selected) p.presetTint else androidx.compose.ui.graphics.Color.Transparent, Square)
            .clickable { onClick() }
            .padding(horizontal = 16.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, 16.sp, if (selected) p.deep else p.text, maxLines = 1)
    }
}

@Composable
fun TabBar(tabs: List<String>, current: String, onPick: (String) -> Unit) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().padding(top = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(0.dp),
    ) {
        tabs.forEach { t ->
            val on = t == current
            Column(
                Modifier.weight(1f).clickable { onPick(t) }.padding(vertical = 12.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(t, 15.sp, if (on) p.accent else p.faint, if (on) FontWeight.SemiBold else FontWeight.Normal)
                Spacer(Modifier.height(8.dp))
                Box(
                    Modifier
                        .fillMaxWidth()
                        .height(if (on) 2.dp else 1.dp)
                        .background(if (on) p.accent else p.lineSoft)
                )
            }
        }
    }
}

@Composable
fun SectionLabel(text: String) {
    val p = palette
    Text(text, 11.sp, p.accent, FontWeight.SemiBold, letterSpacing = 2.sp,
        modifier = Modifier.padding(top = 22.dp, bottom = 2.dp))
}

@Composable
fun Divider(modifier: Modifier = Modifier) {
    val p = palette
    Box(modifier.fillMaxWidth().height(1.dp).background(p.lineSoft))
}
