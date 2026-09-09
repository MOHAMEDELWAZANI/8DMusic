package com.eightd.music

/** Mirrors eightd::Mode in cpp/src/dsp/Params.h. The order is the contract. */
enum class Mode(val label: String) {
    Circular("Circular orbit"),
    PingPong("Ping-pong"),
    Pendulum("Pendulum"),
    Linear("Linear sweep"),
    Figure8("Figure eight"),
    Spiral("Spiral"),
    Random("Random drift"),
    Static("Static position"),
}

/** Mirrors eightd::Character. */
enum class Character(val label: String) {
    Clean("Clean"),
    Slowed("Slowed & sad"),
    Radio("Old radio"),
}

/**
 * The same fields as eightd::Params, with the same defaults. Sent to the audio
 * thread as a flat float[]; the index order is mirrored by ParamIndex in
 * jni_bridge.cpp, so the two halves cannot disagree without failing loudly.
 */
data class Params(
    val enabled: Boolean = true,
    val mode: Mode = Mode.Circular,
    val speed: Float = 0.12f,
    val radius: Float = 1.0f,
    val depth: Float = 0.85f,
    val smoothness: Float = 0.35f,
    val width: Float = 1.0f,
    val direction: Int = 1,
    val manualAngle: Float = 0.0f,
    val pauseWhenSilent: Boolean = true,
    val character: Character = Character.Clean,
    val characterAmount: Float = 1.0f,
    val delayMix: Float = 0.0f,
    val delayTime: Float = 0.28f,
    val delayFeedback: Float = 0.35f,
    val reverbMix: Float = 0.18f,
    val reverbSize: Float = 0.6f,
    val reverbDamp: Float = 0.45f,
    val outputGain: Float = 0.9f,
    /** Three-band tone control in dB. All zero is a true bypass. */
    val eqBass: Float = 0f,
    val eqMid: Float = 0f,
    val eqTreble: Float = 0f,
    /**
     * UI-only. The engine's [enabled] is computed from this and whether the
     * engine is running, so the two can never disagree about which is current.
     */
    val bypass: Boolean = false,
) {
    fun bypassed() = bypass

    fun toFloatArray(): FloatArray = floatArrayOf(
        speed, radius, depth, smoothness, width, manualAngle,
        characterAmount, delayMix, delayTime, delayFeedback,
        reverbMix, reverbSize, reverbDamp, outputGain,
        mode.ordinal.toFloat(), character.ordinal.toFloat(), direction.toFloat(),
        if (enabled) 1f else 0f,
        if (pauseWhenSilent) 1f else 0f,
        eqBass, eqMid, eqTreble,
    )
}

/** The ten presets from cpp/src/dsp/Params.h, value for value. */
object Presets {
    val all: List<Pair<String, Params>> = listOf(
        "Classic 8D" to Params(mode = Mode.Circular, speed = 0.12f, radius = 1.0f, depth = 0.9f,
            smoothness = 0.35f, width = 1.15f, reverbMix = 0.18f, reverbSize = 0.6f, reverbDamp = 0.45f),
        "Slow Orbit" to Params(mode = Mode.Circular, speed = 0.05f, radius = 1.4f, depth = 0.8f,
            smoothness = 0.6f, width = 1.1f, reverbMix = 0.28f, reverbSize = 0.72f, reverbDamp = 0.45f),
        "Ping-Pong" to Params(mode = Mode.PingPong, speed = 0.35f, radius = 0.8f, depth = 1.0f,
            smoothness = 0.25f, width = 1.0f, delayMix = 0.22f, delayTime = 0.22f, delayFeedback = 0.4f,
            reverbMix = 0.12f, reverbSize = 0.6f, reverbDamp = 0.45f),
        "Wide Cinema" to Params(mode = Mode.Pendulum, speed = 0.07f, radius = 1.8f, depth = 0.65f,
            smoothness = 0.75f, width = 1.5f, delayMix = 0.12f, delayTime = 0.4f, delayFeedback = 0.3f,
            reverbMix = 0.4f, reverbSize = 0.82f, reverbDamp = 0.3f),
        "Subtle Motion" to Params(mode = Mode.Pendulum, speed = 0.06f, radius = 1.1f, depth = 0.35f,
            smoothness = 0.8f, width = 1.05f, reverbMix = 0.08f, reverbSize = 0.6f, reverbDamp = 0.45f),
        "Extreme Spin" to Params(mode = Mode.Circular, speed = 0.65f, radius = 0.5f, depth = 1.0f,
            smoothness = 0.1f, width = 1.3f, delayMix = 0.1f, delayTime = 0.15f, delayFeedback = 0.45f,
            reverbMix = 0.2f, reverbSize = 0.6f, reverbDamp = 0.45f),
        "Deep Space" to Params(mode = Mode.Spiral, speed = 0.09f, radius = 2.4f, depth = 0.9f,
            smoothness = 0.65f, width = 1.4f, delayMix = 0.3f, delayTime = 0.5f, delayFeedback = 0.5f,
            reverbMix = 0.55f, reverbSize = 0.88f, reverbDamp = 0.25f),
        "Figure Eight" to Params(mode = Mode.Figure8, speed = 0.15f, radius = 1.2f, depth = 0.95f,
            smoothness = 0.4f, width = 1.2f, delayMix = 0.08f, delayTime = 0.28f, delayFeedback = 0.35f,
            reverbMix = 0.2f, reverbSize = 0.6f, reverbDamp = 0.45f),
        "Slowed & Sad" to Params(mode = Mode.Circular, speed = 0.045f, radius = 1.7f, depth = 0.85f,
            smoothness = 0.82f, width = 1.3f, delayMix = 0.2f, delayTime = 0.6f, delayFeedback = 0.44f,
            reverbMix = 0.52f, reverbSize = 0.86f, reverbDamp = 0.35f,
            character = Character.Slowed, characterAmount = 0.85f),
        "Old Radio" to Params(mode = Mode.Pendulum, speed = 0.05f, radius = 1.15f, depth = 0.4f,
            smoothness = 0.7f, width = 0.4f, reverbMix = 0.24f, reverbSize = 0.5f, reverbDamp = 0.62f,
            character = Character.Radio, characterAmount = 1.0f),
    )
}
