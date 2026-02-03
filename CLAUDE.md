in the page here: web_ui/bluetooth_client/index.html

their is a background thread that is polling for spool updates

but also the user can save settings, and edit spools.

The user sees errors if they try to do an action while other actions are taking place.

update the index.html page so that it has a queue of commands, and does them serially instead of all at once. 