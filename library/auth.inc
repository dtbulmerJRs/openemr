<?php
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

//----------THINGS WE ALWAYS DO

require_once("{$GLOBALS['srcdir']}/log.inc");
require_once("{$GLOBALS['srcdir']}/sql.inc");
// added for the phpGACL group check -- JRM
require_once("{$GLOBALS['srcdir']}/acl.inc");
require_once("$srcdir/formdata.inc.php");
require_once("$srcdir/authentication/login_operations.php");

$incoming_site_id = '';


if(isset($_POST['authPass']))
{
    require_once("authentication/rsa.php");
    $cp=$_POST['authPass'];
    $rsa=new rsa_key_manager();
    $pubKey=$_POST['pk'];
    $rsa->load_from_db($pubKey);
    $clearPass=$rsa->decrypt($cp);
}
if (isset($_GET['auth']) && ($_GET['auth'] == "login") && isset($_POST['authUser']) &&
    isset($_POST['authPass']) && isset($_POST['authProvider']))
{
    // set the language
    if (!empty($_POST['languageChoice'])) {
        $_SESSION['language_choice'] = $_POST['languageChoice'];
    }
    else {
        $_SESSION['language_choice'] = 1;
    }
    
    if(!validate_user_password($_POST['authUser'],$clearPass,$_POST['authProvider']) ||  !verify_user_gacl_group($_POST['authUser']))
    {
        $_SESSION['loginfailure'] = 1;
        authLoginScreen();
    }
//If password expiration option is enabled call authCheckExpired() to check whether login user password is expired or not
    
    if($GLOBALS['password_expiration_days'] != 0){
    	if(authCheckExpired($_POST['authUser']))
        {
            authLoginScreen();
        }
    }
    $ip=$_SERVER['REMOTE_ADDR'];
    $_SESSION['loginfailure'] = null;
    unset($_SESSION['loginfailure']);
    //store the very first initial timestamp for timeout errors
    $_SESSION["last_update"] = time();
}
else if ( (isset($_GET['auth'])) && ($_GET['auth'] == "logout") )
{
    newEvent("logout", $_SESSION['authUser'], $_SESSION['authProvider'], 1, "success");
    authCloseSession();
    authLoginScreen();
}
else
{
    if (authCheckSession())
    {
        if (isset($_SESSION['pid']) && empty($GLOBALS['DAEMON_FLAG']))
        {
            require_once("{$GLOBALS['srcdir']}/patient.inc");
            /**
            $logpatient = getPatientData($_SESSION['pid'], "lname, fname, mname");
            newEvent("view", $_SESSION['authUser'], $_SESSION['authProvider'],
                "{$logpatient['lname']}, {$logpatient['fname']} {$logpatient['mname']} :: encounter " .
                $_SESSION['encounter']);
            **/
        }
        //LOG EVERYTHING
        //newEvent("view", $_SESSION['authUser'], $_SESSION['authProvider'], $_SERVER['REQUEST_URI']);
    }
    else {
        newEvent("login",$_POST['authUser'], $_POST['authProvider'], 0, "insufficient data sent");
        authLoginScreen();
    }
}

if (!isset($_SESSION["last_update"])) {
    authLoginScreen();
} else {
     //if page has not been updated in a given period of time, we call login screen
    if ((time() - $_SESSION["last_update"]) > $timeout) {
        newEvent("logout", $_SESSION['authUser'], $_SESSION['authProvider'], 0, "timeout");
        authCloseSession();
        authLoginScreen();
    } else {
        // Have a mechanism to skip the timeout reset mechanism if a skip_timeout_reset parameter exists. This
        //  can be used by scripts that continually request information from the server; for example the Messages
        //  and Reminders automated intermittent requests that happen in the Messages Center script and in 
        //  the left navigation menu script.
        if (empty($GLOBALS['DAEMON_FLAG']) && empty($_REQUEST['skip_timeout_reset'])) $_SESSION["last_update"] = time();
    }
}

//----------THINGS WE DO IF WE STILL LIKE YOU

function authCheckSession ()
{
    if (isset($_SESSION['authId'])) {
        $authDB = sqlQuery("select username, password from users where id = ?",array($_SESSION['authId']));
        if ($_SESSION['authUser'] == $authDB['username'] )
        {
            return true;
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }
}

function authCloseSession ()
{
  // Before destroying the session, save its site_id so that the next
  // login will default to that same site.
  global $incoming_site_id;
  $incoming_site_id = $_SESSION['site_id'];
  ob_start();
  session_unset();
  session_destroy();
  unset($_COOKIE[session_name()]);
}

function authLoginScreen()
{
  // See comment in authCloseSession().
  global $incoming_site_id;
  header("Location: {$GLOBALS['login_screen']}?error=1&site=$incoming_site_id");
  exit;
}

// Check if the user's password has expired beyond the grace limit.
// If so, deactivate the user
function authCheckExpired($user)
{
  $result = sqlStatement("select pwd_expiration_date from users where username = ?",array($user));
  if($row = sqlFetchArray($result)) 
  {
    $pwd_expires = $row['pwd_expiration_date'];
  }
  $current_date = date("Y-m-d");
  if($pwd_expires != "0000-00-00")
  {
    $grace_time1 = date("Y-m-d", strtotime($pwd_expires . "+".$GLOBALS['password_grace_time'] ."days"));
  }
  if(($grace_time1 != "") && strtotime($current_date) > strtotime($grace_time1))
  {
    sqlStatement("update users set active=0 where username = ?",array($user));
    $_SESSION['loginfailure'] = 1;
    return true;
  }
  return false;
}

function getUserList ($cols = '*', $limit = 'all', $start = '0')
{
    if ($limit = "all")
        $rez = sqlStatement("select $cols from users where username != '' order by date DESC");
    else
        $rez = sqlStatement("select $cols from users where username != '' order by date DESC limit $limit, $start");
    for ($iter = 0; $row = sqlFetchArray($rez); $iter++)
        $tbl[$iter] = $row;
    return $tbl;
}

function getProviderList ($cols = '*', $limit= 'all', $start = '0')
{
    if ($limit = "all")
        $rez = sqlStatement("select $cols from groups order by date DESC");
    else
        $rez = sqlStatement("select $cols from groups order by date DESC limit $limit, $start");
    for ($iter = 0; $row = sqlFetchArray($rez); $iter++)
        $tbl[$iter] = $row;
    return $tbl;
}

function addGroup ($groupname)
{
    return sqlInsert("insert into groups (name) values ('$groupname')");
}

function delGroup ($group_id)
{
    return sqlQuery("delete from groups where id = '$group_id' limit 0,1");
}

/***************************************************************
//pennfirm
//Function currently user by new post calendar code to determine
//if a given user is in a group with another user
//and if so to allow editing of that users events
//
//*************************************************************/

function validateGroupStatus ($user_to_be_checked, $group_user) {
    if (isset($user_to_be_checked) && isset($group_user)) {
        if ($user_to_be_checked == $group_user) {

            return true;
        }
        elseif ($_SESSION['authorizeduser'] == 1)
            return true;

        $query = "SELECT groups.name FROM users,groups WHERE users.username =  \"" . mysql_real_escape_string($user_to_be_checked) . "\" " .
                 "AND users.username = groups.user group by groups.name";
        $result = sqlStatement($query);

        $usertbcGroups = array();

        while ($row = mysql_fetch_array($result)) {
            $usertbcGroups[] = $row[0];
        }

        $query = "SELECT groups.name FROM users,groups WHERE users.username =  \"" . mysql_real_escape_string($group_user) . "\" " .
                 "AND users.username = groups.user group by groups.name";
        $result = sqlStatement($query);

        $usergGroups = array();

        while ($row = mysql_fetch_array($result)) {
            $usergGroups[] = $row[0];
        }
        foreach ($usertbcGroups as $group) {
              if(in_array($group,$usergGroups)) {
              return true;
            }
        }

    }

    return false;
}


// Attempt to update the user's password, password history, and password expiration.
// Verify that the new password does not match the last three passwords used.
// Return true if successfull, false on failure
function UpdatePasswordHistory($userid,$pwd)
{
    $result = sqlStatement("select password, pwd_history1, pwd_history2 from users where id = ?",array($userid));
    if ($row = sqlFetchArray($result)) {
        $previous_pwd1=$row['password'];
        $previous_pwd2=$row['pwd_history1'];
        $previous_pwd3=$row['pwd_history2'];
    }
    if (($pwd != $previous_pwd1) && ($pwd != $previous_pwd2) && ($pwd != $previous_pwd3)) {
        sqlStatement("update users set pwd_history2=?, pwd_history1=?,password=? where id=?",array($previous_pwd2,$previous_pwd1,$pwd,$userid));
	if($GLOBALS['password_expiration_days'] != 0){
	$exp_days=$GLOBALS['password_expiration_days'];
        $exp_date = date('Y-m-d', strtotime("+$exp_days days"));
        sqlStatement("update users set pwd_expiration_date=? where id=?",array($exp_date,$userid));
	}
        return true;
    } 
    else {
        return false;
    }
}
?>
