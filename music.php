<?php

$MSG_ON_END = "END\n";

if (count($_POST)===0) {
	header('Content-Type: text/plain');
	die('This is a testing script for music protocol.  If you do not know what
that means do not worry and just ignore this page.  You should not be
here anyway. ;)');
}


header('Content-Type: text/x-music');


if (empty($_POST['auth'])) {
	die('MUSIC 201 Invalid User
The request is missing authentication parameters.');
}


$auth = explode(':', $_POST['auth']);
if ($auth[0]!=='pass' && $auth[0]!=='open') {
	die('MUSIC 301 Bad Session
This test server does not support sessions.');
}


$user = $auth[1];
$time = $auth[2];
$pass = $auth[3];

if (abs(hexdec($time) - time()) > 24 * 3600) {
	die('MUSIC 203 Invalid Time
Your client has invalid time set.');
}


if ($user!=='mina86' || !check_password($pass, $time)) {
	die('MUSIC 201 Invalid User
Invalid user name or password.
' . $MSG_ON_END);
}


echo("MUSIC 100 OK\n");
if ($auth[0]==='open') {
	echo("SESSION 0 0\n");
}


$songs = empty($_POST['song']) ? array() : $_POST['song'];
$count = count($songs);
for ($i = 0; $i < $count; ++$i) {
	echo('SONG ' . $i . " OK\n");
}


echo("$MSG_ON_END");


function check_password($pass, $time) {
	global $MSG_ON_END;
	$pass = strtr($pass, ' _-', '+++');
	$MSG_ON_END .= "pass: $pass; time: $time\n";
	$MSG_ON_END .= sha1('zaq12wsx') . ' ' . $time . "\n";
	$p = pack('H*', sha1('zaq12wsx')) . $time;
	$p = base64_encode(pack('H*', sha1($p)));
	$MSG_ON_END .= "hash: $p\n";
	return $p===$pass || $p==="$pass=";
}

?>