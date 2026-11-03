#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

hint()
{
    players = get_players();
    if ( players.size )
        players[0] IPrintLnBold( "Flecha abajo / cruceta abajo\nGod mode +10000 | Primera vez: ronda 10" );
}

// Called once per input edge by the native single-player adapter.
press()
{
    players = get_players();
    if ( players.size == 0 || !IsAlive( players[0] ) || !IsDefined( level.round_number ) )
        return;
    player = players[0];
    before = player.score;
    player maps\_zombiemode_score::add_to_player_score( 10000 );
    state = "OFF";
    if ( IsGodMode( player ) ) state = "ON";
    player IPrintLnBold( "God mode: " + state + " | +10000" );
    SetDvar( "delirium_control_event", "god " + state + " points " + before + " -> " + player.score + " round " + level.round_number );
    if ( !IsDefined( level.delirium_round_boost_used ) )
    {
        level.delirium_round_boost_used = true;
        if ( level.round_number < 10 )
            level thread jump_to_ten();
    }
}

jump_to_ten()
{
    // Finish the current wave through BO1's round loop, including the normal
    // intermission and next-wave setup. Do not repeatedly reset running rounds.
    level.zombie_total = 0;
    maps\_zombiemode::ai_calculate_health( 10 );
    level.round_number = 9;
    level notify( "kill_round" );
    SetDvar( "delirium_control_round_requests", 1 );
    wait 1;
    zombies = GetAiSpeciesArray( "axis", "all" );
    for ( i = 0; i < zombies.size; i++ )
        if ( IsDefined( zombies[i] ) && IsAlive( zombies[i] ) )
            zombies[i] DoDamage( zombies[i].health + 666, zombies[i].origin );
    for ( i = 0; i < 600 && level.round_number < 10; i++ )
        wait 0.1;
    SetDvar( "delirium_control_round_reached", level.round_number );
}
