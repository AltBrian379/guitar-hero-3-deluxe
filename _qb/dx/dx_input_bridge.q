; Input mode per controller (devices 0-3 = the four controller slots), read directly from
; memory by GH3 Universal Input (xinput1_3.dll) on PC.
;   0 = Auto (whatever gh3_universal_input.ini says, guitars play as guitars)
;   1 = Guitar  (always treat this controller as a guitar)
;   2 = Gamepad (always treat this controller as a gamepad - no strumming needed)
; Without the DLL (or on console) these values have no effect.
dx_input_mode_device_0 = 0
dx_input_mode_device_1 = 0
dx_input_mode_device_2 = 0
dx_input_mode_device_3 = 0

dx_saved_unplug_check = -1

script dx_load_input_modes
	GetGlobalTags \{user_options}
	if GotParam \{dx_input_mode_device_0}
		Change dx_input_mode_device_0 = <dx_input_mode_device_0>
		Change dx_input_mode_device_1 = <dx_input_mode_device_1>
		Change dx_input_mode_device_2 = <dx_input_mode_device_2>
		Change dx_input_mode_device_3 = <dx_input_mode_device_3>
	endif
endscript

; Returns mode (0-2) and device for a player. device > 3 means not a controller slot (keyboard etc).
script dx_get_player_input_mode \{Player = 1}
	FormatText ChecksumName = player_status 'player%i_status' i = <Player>
	<device> = ($<player_status>.controller)
	<mode> = 0
	switch <device>
		case 0
			<mode> = ($dx_input_mode_device_0)
		case 1
			<mode> = ($dx_input_mode_device_1)
		case 2
			<mode> = ($dx_input_mode_device_2)
		case 3
			<mode> = ($dx_input_mode_device_3)
	endswitch
	return mode = <mode> device = <device>
endscript

script dx_cycle_player_input_mode \{Player = 1}
	dx_get_player_input_mode Player = <Player>
	if (<device> > 3)
		return
	endif
	<mode> = (<mode> + 1)
	if (<mode> > 2)
		<mode> = 0
	endif
	switch <device>
		case 0
			Change dx_input_mode_device_0 = <mode>
			SetGlobalTags user_options Params = {dx_input_mode_device_0 = <mode>}
		case 1
			Change dx_input_mode_device_1 = <mode>
			SetGlobalTags user_options Params = {dx_input_mode_device_1 = <mode>}
		case 2
			Change dx_input_mode_device_2 = <mode>
			SetGlobalTags user_options Params = {dx_input_mode_device_2 = <mode>}
		case 3
			Change dx_input_mode_device_3 = <mode>
			SetGlobalTags user_options Params = {dx_input_mode_device_3 = <mode>}
	endswitch
	; The DLL may briefly report the controller as unplugged so GH3 re-detects its type;
	; don't let that pop up the "controller disconnected" screen.
	KillSpawnedScript \{Name = dx_input_mode_reconnect_guard}
	SpawnScriptNow \{dx_input_mode_reconnect_guard}
endscript

script dx_input_mode_reconnect_guard
	if ($dx_saved_unplug_check = -1)
		Change dx_saved_unplug_check = ($check_for_unplugged_controllers)
	endif
	Change \{check_for_unplugged_controllers = 0}
	Wait \{1.5 Seconds}
	Change check_for_unplugged_controllers = ($dx_saved_unplug_check)
	Change \{dx_saved_unplug_check = -1}
endscript

script dx_input_mode_text \{Player = 1}
	dx_get_player_input_mode Player = <Player>
	if (<device> > 3)
		return \{text = 'N/A'}
	endif
	switch <mode>
		case 1
			return \{text = 'Guitar'}
		case 2
			return \{text = 'Gamepad'}
	endswitch
	return \{text = 'Auto'}
endscript
