<?php
session_start();
define("ZWAVE_HOST", "localhost");
define("ZWAVE_PORT", 60004);

function sendReceiveMessage($in)
{
    $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
    if ($socket === false) {
        return "ERROR: socket_create() failed: reason: " . socket_strerror(socket_last_error()) . "\n";
    }

    $result = socket_connect($socket, ZWAVE_HOST, ZWAVE_PORT);
    if ($result === false) {
        return "ERROR: socket_connect() failed.\nReason: ($result) " . socket_strerror(socket_last_error($socket)) . "\n";
    }
    socket_write($socket, $in, strlen($in));
    //TODO: should really change this to keep getting until \n
    $data=socket_read($socket, 2048,PHP_NORMAL_READ );
    socket_close($socket);
    usleep(250); //this just makes it happy
    return $data;
}

function opposite($str)
{
    if($str=='On')
        return 'Off';
    else
        return 'On';
}

function getDevices()
{
    $list=sendReceiveMessage('ALIST');
    $list = substr($list, 0, strlen($list) - 1);  //doing this instead of trim

    $devicesList = explode("#", $list);
    $devices = array();
    $c=0;
    foreach ($devicesList as $device) {
            $device = explode("~", $device);

            $devices[$c]['name']=$device[1] ;
            $devices[$c]['node']=$device[2] ;
            $devices[$c]['group']=$device[3] ;
            $devices[$c]['type']=$device[4] ;
            if(strpos($device[5],'Basic=255')!==false)
            {
                         $devices[$c]['status']='On';
            }
            if(strpos($device[5],'Basic=0')!==false)
            {
                         $devices[$c]['status']='Off';
            }
            if($device[4]=='Multilevel Power Switch')
            {
                $temp=explode(' Basic=',$device[5]);
                $temp= explode(' ',$temp[1]);
                $devices[$c]['status']=$temp[0];
            }
                $c++;
                }

    //var_dump($devices);
        return $devices;
}
$devices=getDevices();

if($node=filter_input(INPUT_POST,'node_node',FILTER_VALIDATE_INT))
{

    //DEVICE~2~255~Binary Switch
    //SETNODE~2~TESTLAMP~1
    //
    if($_POST['node_type']=='Multilevel Power Switch')
    {
      $msg= "DEVICE~$node~". $_POST['node_status'] . "~". $_POST['node_type'];
        
    }
    if($_POST['node_type']=='Binary Power Switch' || $_POST['node_type']=='Binary Switch')
    {
        $msg="DEVICE~$node~0~Binary Switch";
        if($_POST['node_status']=='On')
        {
            $msg="DEVICE~$node~255~Binary Switch";
        }
    }
    //don't resubmit
    if($devices[$node-2]['status']!=$_POST['node_status'])
    {
        $_SESSION['msg']=sendReceiveMessage($msg);
        sleep(1);
    }
//not really the best way to get the node name (assumes no other controller is in the way)
    if($_POST['node_name'] != $devices[$node-2]['name'] ||  $_POST['node_group'] != $devices[$node-2]['group'])
    {

        $_SESSION['msg'].= ", " . sendReceiveMessage("SETNODE~". $node . "~" . $_POST['node_name']  ."~" . $_POST['node_group'] . "~");

    }
    //sleep(2);// likes it
    //$devices=getDevices();
    header('Location: /');
}

///////////spit out template
?>
<head>
<style type="text/css">
    body{
        font-family: arial;
        font-size: 12px;
        line-height: 1.3em;

    }
    input[type=text] {
        width:115px;
    }
    label {
        width:45px;float:left;
    }
    .node{
        float:left; width:auto;min-width:280px; margin:5px;padding:15px;padding-right:10px;padding-left:10px;border:2px solid gray;border-radius: 15px;-moz-border-radius: 15px;background-color: #f5f5f5;
    }
    .pictureframe{
        float:left;margin-right:8px;border:1px solid #777777;border-radius: 5px;-moz-border-radius: 5px;background-color: white;
    }
    .field_container{
        margin-bottom: 2px;
    }
    #message{
        width:280px;
        margin-left:auto;
        margin-right: auto;
        margin-top:20px;
    }
</style>
    <title>Lights</title>
    <link rel="apple-touch-icon" href="/lights.png"/>
    </head>
    <body>
<div id="content" style="width:auto;margin:auto; padding:5px;">
<?php
foreach($devices as $device)
{

    echo "<div class='node'><form method='post'><div class='pictureframe'><input type='image' width='100px' height='133px' src='images/node_" . $device['node'] . "_" . strtolower($device['status']) .".jpg' border='0' style='margin:2px;'></div>
    <div class='field_container'><label>Name:</label><input type='text' name='node_name' value='" . $device['name'] . "'></div>
     <div class='field_container'><label>Type:</label><input type='text' name='node_type' value='" . $device['type'] . "' disabled></div>
        <div class='field_container'><label>Group:</label><input type='text' name='node_group' value='" . $device['group'] . "'></div>

    <div class='field_container'><label>Status: </label>";
    
    //horrible hack
    if($device['type']=='Multilevel Power Switch')
    {
        
         echo "<input type=text name='node_status' value='" . $device['status'] . "'></div>";
    }
    else{
        
    
        echo "<input type=text value='" . $device['status'] . "' disabled></div><input type='hidden' name='node_status' value='" . opposite($device['status']) . "'>";
    }
echo "<input type='hidden' name='node_node' value='" . $device['node'] . "'>
    </form></div> ";
}

?>
</div>
<?php
if($_SESSION['msg'])
{
    echo "<div style='clear:both'></div> <div id='message'><stong>Last MSG: </stong>" , $_SESSION['msg'], "</div>";
}
?>
    </body>
