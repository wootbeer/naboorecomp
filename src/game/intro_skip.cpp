// Start skips the intro (the N64, LucasArts and Factor 5 logos: level 20), which the game
// itself won't do (naboo.us.rev0.toml, hooks in func_41001FC4 and func_4100096C;
// STATUS.md round 21). Set when Start asks the level to end, and used once where the scene
// loop decides to fade out, which otherwise waits for the logos' camera script to finish.
extern "C" {
    int naboo_intro_skip = 0;
}
