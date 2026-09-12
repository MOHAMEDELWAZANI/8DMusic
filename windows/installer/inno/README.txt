8D MUSIC
Real-time spatial audio for everything your computer plays
================================================================

WHAT IT DOES

8D Music makes sound seem to move around your head. Once it is installed it
works on everything -- Spotify, YouTube, games, films, video calls -- without
you having to change any setting in those programs.

Use headphones. The effect is built on the difference between what your left
and right ear hear, so speakers will not do it justice.


================================================================
BEFORE YOU INSTALL -- PLEASE READ THIS

Three things you should know.

1. YOU NEED ADMINISTRATOR ACCESS.
   The effect has to be installed into Windows' own audio system, and Windows
   will not let an ordinary program do that.

2. YOU NEED TO RESTART YOUR COMPUTER AFTERWARDS.
   Windows only picks up a new audio effect properly after a restart. Until you
   restart, you may hear nothing different.

3. IT CHANGES A WINDOWS SECURITY SETTING.
   This is the important one, so here it is plainly.

   Windows normally refuses to load audio effects unless they have been signed
   by Microsoft. 8D Music is not signed, so the installer switches that check
   off. The setting is called DisableProtectedAudioDG.

   What that means for you:

     - The setting applies to your whole computer, not just to 8D Music. Any
       other unsigned audio effect could load too.
     - Some copy-protected music and video -- certain streaming services, some
       DVD and Blu-ray software -- may refuse to play, or may play without
       sound, while this setting is off. This is the protected content noticing
       the check is disabled. It is not damage; it goes away when you uninstall.

   The uninstaller puts the setting back exactly as it found it. If some other
   audio software had already turned it off before you installed 8D Music, the
   uninstaller leaves it off rather than breaking that software.

   If you are not comfortable with this, do not install. Nothing here is
   hidden from you, and that is deliberate.


================================================================
HOW TO INSTALL

1. Right-click  Install.bat
2. Choose      "Run as administrator"
3. Click Yes when Windows asks.
4. Read what it prints, then RESTART YOUR COMPUTER.
5. After the restart, open:
       C:\Program Files\8DMusic\8DMusic.exe
6. Play some music. The window should say "Processing system audio" in green.

If it says "Waiting", nothing is playing yet. Start a song and it will change.


================================================================
HOW TO UNINSTALL

1. Right-click  Uninstall.bat
2. Choose      "Run as administrator"
3. Restart your computer.

The uninstaller restores every setting it changed, from a record it wrote
during installation. It works even if your sound has stopped working, because
it only edits settings -- it does not need audio to be running.


================================================================
IF SOMETHING GOES WRONG

If you have no sound at all:

  This is the thing to try first. Open PowerShell as Administrator
  (right-click the Start button, choose "Terminal (Admin)" or
  "Windows PowerShell (Admin)") and paste this in:

      reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio" /v DisableProtectedAudioDG /t REG_DWORD /d 0 /f
      Restart-Service audiosrv -Force

  That tells Windows to refuse unsigned audio effects again, which switches
  8D Music off and brings your sound straight back. Nothing is uninstalled, so
  you lose nothing by trying it. Then run Uninstall.bat when convenient.

If your computer will not start properly:

  Start Windows in Safe Mode and run Uninstall.bat there. The audio system does
  not run in Safe Mode, so the effect is never loaded and the uninstaller has a
  clear run at it.

If you would rather do it by hand:

  Everything that was changed is listed in
      C:\ProgramData\8DMusic\install-backup.json


================================================================
THINGS IT DOES NOT DO, AND WHY

It does not change your output device. Whatever you had selected stays
selected.

Each program gets its own copy of the effect. If two programs play at once,
each one orbits on its own rather than the two moving together as one scene.
That is a limitation of where Windows lets us sit in the audio path.

It only works on stereo output. If a device is set to 5.1 or 7.1 surround, the
effect steps aside and passes the sound through untouched -- the window will
tell you so, rather than leaving you guessing.

Some devices cannot take an audio effect at all. The installer says which ones
it skipped and does not touch them.

"Now playing" only shows programs that tell Windows what they are playing.
Spotify and web browsers do. Discord and most games do not, so it may say
"Nothing playing" while you can plainly hear something. That is a limit of what
Windows makes available, not a fault.


================================================================
WHAT GETS INSTALLED WHERE

  C:\Program Files\8DMusic\        the effect and the control window
  C:\ProgramData\8DMusic\          your settings, and the record of what
                                   the installer changed

Nothing runs at startup. Nothing connects to the internet. The control window
is only needed when you want to change something -- the effect keeps working
after you close it.
