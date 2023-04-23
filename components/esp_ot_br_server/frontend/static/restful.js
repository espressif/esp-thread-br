var OT_SERVER_PACKAGE_VERSION = "v1.0.0";
/* --------------------------------------------------------------------
                            action
-------------------------------------------------------------------- */
function click_for_more_form_param() {
  elem = document.getElementById("form-more-param");
  if (elem.style.display == 'block') {
    elem.style.display = 'none';
    document.getElementById('form-more-tip').innerHTML = "for more &#x21B5;";
  } else {
    elem.style.display = 'block';
    document.getElementById('form-more-tip').innerHTML = "for less &#x21B5;";
  }
}

function click_copy_network_info_to_form(arg) {
  var row = $(arg).parent().parent().find("td");
  if (row.eq(0) == "")
    return;
  var data = {
    id : row.eq(0).text(),
    network_name : row.eq(1).text(),
    extended_panid : row.eq(2).text(),
    panid : row.eq(3).text(),
    mac_address : row.eq(4).text(),
    channel : row.eq(5).text(),
    dBm : row.eq(6).text(),
    LQI : row.eq(7).text(),
  };

  document.getElementsByName("network_name")[0].value = data.network_name;
  document.getElementsByName("extended_panid")[0].value = data.extended_panid;
  document.getElementsByName("panid")[0].value = data.panid;
  document.getElementsByName("channel")[0].value = data.channel;

  item = document.getElementById("form_tip");
  item.style.color = "blue";
  item.style.display = "block";
  item.innerHTML = "Form update."
}
/* --------------------------------------------------------------------
                            discover
-------------------------------------------------------------------- */
$("document").ready(function() {
  $("div ul li a").click(function() {
    $("div ul li a").removeClass("active"); // firstly ,remove all actives
    $(this).addClass("active"); // choose 'li' to add option 'active'

    var tabx = $(this).attr('id');
    tabx = tabx.slice(5, 6);
    console.log(tabx);
    var panes = document.querySelectorAll(".tab-pane");
    for (i = 0; i < panes.length; i++) {
      panes[i].className = "tab-pane";
    }
    panes[tabx].className = "tab-pane active";
  });
});

function fill_avai_thread_network_table(data) {

  document.getElementById("available_networks_body").innerHTML =
      "<tr><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td></tr>"; // clear table
  var rows = '';
  var row_id = 1;
  if (data.content.threadNetworks == false)
    return;
  data.content.threadNetworks.forEach(function(keys) {
    rows += '<tr>'
    for (var k in keys) {
      rows += '<td>' + keys[k] + '</td>'
    }
    rows += '<td>'
    rows +=
        "<button class=\"btn-submit\" onclick=\"click_copy_network_info_to_form(this)\">FILL<\/button>"
    rows += '</td>'
    rows += '</tr>'
    row_id++;
  });

  document.getElementById("available_networks_table").caption.innerText =
      "Available Thread Networks: Scan Completed"
  document.getElementById("available_networks_body").innerHTML = rows;
}
function get_threadScan_httpServer() {
  document.getElementById("available_networks_table").caption.innerText =
      "Available Thread Networks: Waiting ..."

  $.ajax({
    url : '/thread/api/scan',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'GET',
    dataType : "json",
    data : "",
    success : function(arg) {
      console.log(arg);
      fill_avai_thread_network_table(arg);
    },
    error : function(arg) { console.log(arg) }
  })
}
/* --------------------------------------------------------------------
                            form
-------------------------------------------------------------------- */
function handle_form_response_message(arg, form_id) {
  item = document.getElementById(form_id);
  if (arg.hasOwnProperty("content")) {
    if (arg.content.hasOwnProperty("fail")) {
      item.style.color = "red";
      item.innerHTML = arg.content.fail;
    } else {
      item.style.color = "green";
      item.innerHTML = arg.content.success;
    }
  } else {
    item.style.color = "red";
    item.innerHTML = "Try against.";
  }
}

/* convert form's input to json type */
$.fn.serializeJson =
    function() {
  var serializeObj = {};
  var array = this.serializeArray();
  var str = this.serialize();
  $(array).each(function() {
    if (serializeObj[this.name]) {
      if ($.isArray(serializeObj[this.name])) {
        serializeObj[this.name].push(this.value);
      } else {
        serializeObj[this.name] = [ serializeObj[this.name], this.value ];
      }
    } else {
      serializeObj[this.name] = this.value;
    }
  });
  return serializeObj;
}

function upload_form_network_param_package() {
  item = document.getElementById("form_tip");
  item.style.color = "green";
  item.style.display = 'block';

  var root = {
    version : OT_SERVER_PACKAGE_VERSION,
    content : {
      form : $("#network_form").serializeJson(),
    }
  };

  $.ajax({
    url : '/thread/api/form',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'POST',
    dataType : "json",
    data : JSON.stringify(root),
    success : function(arg) {
      console.log(arg);
      if (arg != {})
        handle_form_response_message(arg, "form_tip");
    },
    error : function(arg) { console.log(arg) }
  })
}

/* --------------------------------------------------------------------
                            Status
-------------------------------------------------------------------- */
function decode_thread_status_package(package) {
  if (package.version == OT_SERVER_PACKAGE_VERSION) {
    document.getElementById("ipv6-link_local_address").innerHTML =
        package.content.ipv6.link_local_address;
    document.getElementById("ipv6-routing_local_address").innerHTML =
        package.content.ipv6.routing_local_address;
    document.getElementById("ipv6-mesh_local_address").innerHTML =
        package.content.ipv6.mesh_local_address;
    document.getElementById("ipv6-mesh_local_prefix").innerHTML =
        package.content.ipv6.mesh_local_prefix;

    document.getElementById("network-name").innerHTML =
        package.content.network.name;
    document.getElementById("network-panid").innerHTML =
        package.content.network.panid;
    document.getElementById("network-partition_id").innerHTML =
        package.content.network.partition_id;
    document.getElementById("network-xpanid").innerHTML =
        package.content.network.xpanid;

    document.getElementById("openthread-version").innerHTML =
        package.content.information.version;
    document.getElementById("openthread-version_api").innerHTML =
        package.content.information.version_api;
    document.getElementById("t-openthread-state").innerHTML =
        package.content.information.role;
    document.getElementById("openthread-PSKc").innerHTML =
        package.content.information.PSKc;

    document.getElementById("rcp-channel").innerHTML =
        package.content.rcp.channel;
    document.getElementById("rcp-EUI64").innerHTML = package.content.rcp.EUI64;
    document.getElementById("rcp-txpower").innerHTML =
        package.content.rcp.txpower;
    document.getElementById("rcp-version").innerHTML =
        package.content.rcp.version;

    document.getElementById("WPAN-service").innerHTML =
        package.content.wpan.service;

    document.getElementById("t-ipv6-link_local_address").innerHTML =
        package.content.ipv6.link_local_address;
    document.getElementById("ipv6-routing_local_address").innerHTML =
        package.content.ipv6.routing_local_address;
    document.getElementById("t-ipv6-mesh_local_address").innerHTML =
        package.content.ipv6.mesh_local_address;
    document.getElementById("t-ipv6-mesh_local_prefix").innerHTML =
        package.content.ipv6.mesh_local_prefix;

    document.getElementById("t-network-name").innerHTML =
        package.content.network.name;
    document.getElementById("t-network-panid").innerHTML =
        package.content.network.panid;
    document.getElementById("t-network-partition_id").innerHTML =
        package.content.network.partition_id;
    document.getElementById("t-network-xpanid").innerHTML =
        package.content.network.xpanid;

    document.getElementById("t-openthread-version").innerHTML =
        package.content.information.version;
    document.getElementById("t-openthread-version_api").innerHTML =
        package.content.information.version_api;
    document.getElementById("t-openthread-state").innerHTML =
        package.content.information.role;
    document.getElementById("t-openthread-PSKc").innerHTML =
        package.content.information.PSKc;

    document.getElementById("t-rcp-channel").innerHTML =
        package.content.rcp.channel;
    document.getElementById("t-rcp-EUI64").innerHTML =
        package.content.rcp.EUI64;
    document.getElementById("t-rcp-txpower").innerHTML =
        package.content.rcp.txpower;
    document.getElementById("t-rcp-version").innerHTML =
        package.content.rcp.version;

    document.getElementById("t-WPAN-service").innerHTML =
        package.content.wpan.service;
  }
} function get_threadStatus_httpServer() {
  $.ajax({
    url : '/thread/api/status',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'GET',
    dataType : "json",
    data : "",
    success : function(arg) {
      console.log(arg);
      decode_thread_status_package(arg);
    },
    error : function(arg) { console.log(arg) }
  })
}
