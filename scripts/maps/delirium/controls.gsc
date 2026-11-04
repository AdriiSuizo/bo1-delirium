#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

hint()
{
    IPrintLnBold( "Cruceta / flechas: abajo Dios y ronda 20 | arriba pausa | izquierda +5 | derecha persecucion" );
}

press()
{
    players = get_players();
    if ( !players.size || !IsAlive( players[0] ) || !IsDefined( level.round_number ) )
        return;

    player = players[0];
    before = player.score;
    player maps\_zombiemode_score::add_to_player_score( 100000 );
    state = "OFF";
    if ( IsGodMode( player ) ) state = "ON";
    player IPrintLnBold( "God mode: " + state + " | +100000" );
    SetDvar( "delirium_control_event", "god " + state + " points " + before + " -> " + player.score + " round " + level.round_number );
    if ( level.round_number < 20 && !is_true( level.delirium_round_jump ) )
    {
        level.delirium_round_jump = true;
        level thread jump_to_twenty();
    }
}

kill_live_zombies()
{
    zombies = GetAiSpeciesArray( "axis", "all" );
    killed = 0;
    for ( i = 0; i < zombies.size; i++ )
    {
        if ( IsDefined( zombies[i] ) && IsAlive( zombies[i] ) )
        {
            zombies[i] DoDamage( zombies[i].health + 666, zombies[i].origin );
            killed++;
        }
    }
    return killed;
}

jump_to_twenty()
{
    while ( is_true( level.delirium_round_paused ) )
        wait 0.1;
    if ( level.round_number < 20 )
    {
        level.zombie_total = 0;
        maps\_zombiemode::ai_calculate_health( 20 );
        level.round_number = 19;
        level notify( "kill_round" );
        wait 1;
        kill_live_zombies();
        for ( i = 0; i < 600 && level.round_number < 20; i++ )
            wait 0.1;
    }
    level.delirium_round_jump = false;
    SetDvar( "delirium_control_round_reached", level.round_number );
}

pause_round()
{
    if ( !IsDefined( level.zombie_total ) )
        return;
    if ( is_true( level.delirium_round_paused ) )
    {
        level.delirium_round_paused = false;
        if ( is_true( level.delirium_held_empty_round ) )
            level.zombie_total = 0;
        if ( is_true( level.delirium_restore_spawning ) )
            flag_set( "spawn_zombies" );
        IPrintLnBold( "Ronda reanudada" );
        SetDvar( "delirium_control_event", "round resumed " + level.round_number );
        return;
    }

    level.delirium_round_paused = true;
    level.delirium_restore_spawning = flag( "spawn_zombies" );
    flag_clear( "spawn_zombies" );
    // The stock round completion test requires either a live actor or a
    // queued spawn. Preserve the current round even after its last spawn.
    level.delirium_held_empty_round = level.zombie_total <= 0;
    if ( level.delirium_held_empty_round ) level.zombie_total = 1;
    killed = kill_live_zombies();
    IPrintLnBold( "Ronda pausada | Eliminados: " + killed );
    SetDvar( "delirium_control_event", "round paused " + level.round_number + " killed " + killed );
}

spawn_five()
{
    if ( is_true( level.delirium_spawning ) )
        return;

    spawners = [];
    if ( IsDefined( level.enemy_spawns ) && level.enemy_spawns.size )
        spawners = level.enemy_spawns;
    else
    {
        // Shangri-La uses one stock factory instead of level.enemy_spawns.
        // Its normal factory is marked zombie_spawner; special enemies use
        // separate noteworthy values and must keep their own map scripts.
        candidates = GetSpawnerArray();
        for ( i = 0; i < candidates.size; i++ )
        {
            if ( IsDefined( candidates[i] ) && IsDefined( candidates[i].script_noteworthy ) && candidates[i].script_noteworthy == "zombie_spawner" )
                spawners[spawners.size] = candidates[i];
        }
    }
    if ( !spawners.size )
        return;

    level.delirium_spawning = true;
    spawned = 0;
    for ( attempt = 0; attempt < 25 && spawned < 5; attempt++ )
    {
        if ( spawners.size && IsDefined( spawners[attempt % spawners.size] ) )
        {
            zombie = spawn_zombie( spawners[attempt % spawners.size] );
            if ( IsDefined( zombie ) )
                spawned++;
        }
        wait 0.1;
    }
    level.delirium_spawning = false;
    IPrintLnBold( "Zombis de prueba: " + spawned );
    SetDvar( "delirium_control_event", "manual spawned " + spawned + " round " + level.round_number + " paused " + is_true( level.delirium_round_paused ) );
}

pursuit()
{
    players = get_players();
    if ( !players.size )
        return;
    state = "ON";
    if ( players[0] IsNoTarget() )
        state = "OFF";
    players[0] IPrintLnBold( "Persecucion: " + state );
    SetDvar( "delirium_control_event", "pursuit " + state );
}
