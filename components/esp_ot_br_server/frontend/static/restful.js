var OT_SERVER_PACKAGE_VERSION = "v1.0.0";
/* --------------------------------------------------------------------
                            action
-------------------------------------------------------------------- */
function frontend_click_for_more_form_param() {
  elem = document.getElementById("form-more-param");
  if (elem.style.display == 'block') {
    elem.style.display = 'none';
    document.getElementById('form-more-tip').innerHTML = "for more &#x21B5;";
  } else {
    elem.style.display = 'block';
    document.getElementById('form-more-tip').innerHTML = "for less &#x21B5;";
  }
}

function frontend_click_copy_network_info_to_form(arg) {
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

  document.getElementsByName("networkName")[0].value = data.network_name;
  document.getElementsByName("extPanId")[0].value = data.extended_panid;
  document.getElementsByName("panId")[0].value = data.panid;
  document.getElementsByName("channel")[0].value = data.channel;

  item = document.getElementById("form_tip");
  item.style.color = "blue";
  item.style.display = "block";
  item.innerHTML = "Form update."
}

function frontend_log_show(title, arg) {

  document.getElementById("log_window_title").innerText = title;
  document.getElementById("log_window_title").style.fontSize = "25px";

  if (!arg.hasOwnProperty("error") || !arg.hasOwnProperty("content")) {
    document.getElementById("log_window").style.display = "flex";
    document.getElementById("log_window_content").innerText = "Unknown: ";
    return;
  }
  if (arg.error == 0)
    document.getElementById("log_window_content").style.color = "green";
  else
    document.getElementById("log_window_content").style.color = "red";

  document.getElementById("log_window").style.display = "flex";
  document.getElementById("log_window_content").innerText = arg.content;
  return;
}

function frontend_log_close() {
  document.getElementById("log_window").style.display = "none";
}

function console_show_response_result(arg) {
  console.log("Error: ", arg.error);
  console.log("Result: ", arg.result);
  console.log("Message: ", arg.message);
}
/* --------------------------------------------------------------------
                            Discover
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

function fill_thread_available_network_table(data) {
  document.getElementById("available_networks_body").innerHTML =
      "<tr><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td></tr>"; // clear table
  var rows = '';
  var row_id = 1;
  if (data.error)
    return;
  data.result.forEach(function(keys) {
    rows += '<tr>'
    for (var k in keys) {
      rows += '<td>' + keys[k] + '</td>'
    }
    rows += '<td>'
    rows +=
        "<button class=\"btn-submit\" onclick=\"frontend_show_join_network_window(this)\">Join<\/button>"
    rows += '</td>'
    rows += '</tr>'
    row_id++;
  });

  document.getElementById("available_networks_table").caption.innerText =
      "Available Thread Networks: Scan Completed"
  document.getElementById("available_networks_body").innerHTML = rows;
}

function http_server_scan_thread_network() {
  var log = {error : 0, content : ""};
  var title = "Available Network";

  document.getElementById("available_networks_table").caption.innerText =
      "Available Thread Networks: Waiting ..."

  log.content = "Waiting...";
  frontend_log_show(title, log);

  $.ajax({
    url : '/available_network',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'GET',
    dataType : "json",
    data : "",
    success : function(arg) {
      console_show_response_result(arg);
      fill_thread_available_network_table(arg);
      log.error = arg.error;
      log.content = arg.message;
      frontend_log_show(title, log);
    },
    error : function(arg) {
      log.error = "Error: ";
      log.content = "Unknown: ";
      frontend_log_show(title, log);
      console.log(arg);
    }
  })
}

/* --------------------------------------------------------------------
                            Join
-------------------------------------------------------------------- */
var g_available_networks_row;
function http_server_join_thread_network(root) {
  var log = {error : 0, content : ""};
  var title = "Join"
  $.ajax({
    url : '/join_network',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'POST',
    dataType : "json",
    data : JSON.stringify(root),
    success : function(arg) {
      console_show_response_result(arg);
      log.error = arg.error;
      log.content = arg.message;
      frontend_log_show(title, log);
    },
    error : function(arg) {
      log.error = "Error";
      log.content = "Unknown";
      frontend_log_show(title, log);
      console.log(arg)
    }
  })
}

function frontend_show_join_network_window(arg) {
  g_available_networks_row = $(arg).parent().parent().find("td");
  document.getElementById('join_window').style.display = 'block';
}

function frontend_submit_join_network(arg) {
  if (g_available_networks_row == "" || g_available_networks_row.eq(0) == "") {
    console.log("Invalid Network!");
    return;
  }
  var root = $("#join_network_table").serializeJson();
  root.index = parseInt(g_available_networks_row.eq(0).text());
  if (root.hasOwnProperty("defaultRoute") && root.defaultRoute == "on")
    root.defaultRoute = 1;
  else
    root.defaultRoute = 0;

  http_server_join_thread_network(root);
  document.getElementById('join_window').style.display = "none"
}

function frontend_cancel_join_network(data) {
  var item = document.getElementById('join_window');
  item.style.display = "none"
  return false;
}

function frontend_join_type_select(data) {
  if (data.options[data.selectedIndex].value == "network_key_type") {
    document.getElementById('join_network_key').style.display = 'block'
    document.getElementById('join_thread_pskd').style.display = 'none'
  } else if (data.options[data.selectedIndex].value == "thread_pskd_type") {
    document.getElementById('join_network_key').style.display = 'none'
    document.getElementById('join_thread_pskd').style.display = 'block'
  }
}

/* --------------------------------------------------------------------
                            Form
-------------------------------------------------------------------- */
function handle_form_response_message(arg, form_id) {
  item = document.getElementById(form_id);
  if (arg.hasOwnProperty("error") && !arg.error) {
    if (arg.result == "successful") {
      item.style.color = "green";
      item.innerHTML = arg.message;
    } else {
      item.style.color = "red";
      item.innerHTML = arg.message;
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

function http_server_upload_form_network_table() {
  item = document.getElementById("form_tip");
  item.style.color = "green";
  item.style.display = 'block';

  var root = $("#network_form").serializeJson();
  var title = "Form";
  if (root.hasOwnProperty("defaultRoute") && root.defaultRoute == "on")
    root.defaultRoute = 1;
  else
    root.defaultRoute = 0;
  if (root.hasOwnProperty("defaultRoute") && root.defaultRoute != "")
    root.channel = parseInt(root.channel);

  var log = {error : 0, content : ""};

  $.ajax({
    url : '/form_network',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'POST',
    dataType : "json",
    data : JSON.stringify(root),
    success : function(arg) {
      console_show_response_result(arg);
      if (arg != {})
        handle_form_response_message(arg, "form_tip");
      log.error = arg.error;
      log.content = arg.message;
      frontend_log_show(title, log);
    },
    error : function(arg) {
      log.error = "Error: ";
      log.content = "Unknown: ";
      frontend_log_show(title, log);
      console.log(arg)
    }
  })
}

/* --------------------------------------------------------------------
                            Status
-------------------------------------------------------------------- */
function decode_thread_status_package(package) {
  if (package.error)
    return;

  document.getElementById("ipv6-link_local_address").innerHTML =
      package.result["IPv6:LinkLocalAddress"];
  document.getElementById("ipv6-routing_local_address").innerHTML =
      package.result["IPv6:RoutingLocalAddress"];
  document.getElementById("ipv6-mesh_local_address").innerHTML =
      package.result["IPv6:MeshLocalAddress"];
  document.getElementById("ipv6-mesh_local_prefix").innerHTML =
      package.result["IPv6:MeshLocalPrefix"];

  document.getElementById("network-name").innerHTML =
      package.result["Network:Name"];
  document.getElementById("network-panid").innerHTML =
      package.result["Network:PANID"];
  document.getElementById("network-partition_id").innerHTML =
      package.result["Network:PartitionID"];
  document.getElementById("network-xpanid").innerHTML =
      package.result["Network:XPANID"];
  document.getElementById("network-baid").innerHTML =
      package.result["Network:BorderAgentID"];

  document.getElementById("openthread-version").innerHTML =
      package.result["OpenThread:Version"];
  document.getElementById("openthread-version_api").innerHTML =
      package.result["OpenThread:Version API"];
  document.getElementById("openthread-role").innerHTML =
      package.result["RCP:State"];
  document.getElementById("openthread-PSKc").innerHTML =
      package.result["OpenThread:PSKc"];

  document.getElementById("rcp-channel").innerHTML =
      package.result["RCP:Channel"];
  document.getElementById("rcp-EUI64").innerHTML = package.result["RCP:EUI64"];
  document.getElementById("rcp-txpower").innerHTML =
      package.result["RCP:TxPower"];
  document.getElementById("rcp-version").innerHTML =
      package.result["RCP:Version"];

  document.getElementById("WPAN-service").innerHTML =
      package.result["WPAN service"];

  document.getElementById("t-ipv6-link_local_address").innerHTML =
      package.result["IPv6:LinkLocalAddress"];
  document.getElementById("t-ipv6-routing_local_address").innerHTML =
      package.result["IPv6:RoutingLocalAddress"];
  document.getElementById("t-ipv6-mesh_local_address").innerHTML =
      package.result["IPv6:MeshLocalAddress"];
  document.getElementById("t-ipv6-mesh_local_prefix").innerHTML =
      package.result["IPv6:MeshLocalPrefix"];

  document.getElementById("t-network-name").innerHTML =
      package.result["Network:Name"];
  document.getElementById("t-network-panid").innerHTML =
      package.result["Network:PANID"];
  document.getElementById("t-network-partition_id").innerHTML =
      package.result["Network:PartitionID"];
  document.getElementById("t-network-xpanid").innerHTML =
      package.result["Network:XPANID"];
  document.getElementById("t-network-baid").innerHTML =
      package.result["Network:BorderAgentID"];

  document.getElementById("t-openthread-version").innerHTML =
      package.result["OpenThread:Version"];
  document.getElementById("t-openthread-version_api").innerHTML =
      package.result["OpenThread:Version API"];
  document.getElementById("t-openthread-role").innerHTML =
      package.result["RCP:State"];
  document.getElementById("t-openthread-PSKc").innerHTML =
      package.result["OpenThread:PSKc"];

  document.getElementById("t-rcp-channel").innerHTML =
      package.result["RCP:Channel"];
  document.getElementById("t-rcp-EUI64").innerHTML = package.result["RCP:EUI64"]
  document.getElementById("t-rcp-txpower").innerHTML =
      package.result["RCP:TxPower"];
  document.getElementById("t-rcp-version").innerHTML =
      package.result["RCP:Version"];

  document.getElementById("t-WPAN-service").innerHTML =
      package.result["WPAN service"];
}

function http_server_get_thread_network_properties() {
  var log = {error : 0, content : ""};
  var title = "Properties";
  $.ajax({
    url : '/get_properties',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'GET',
    dataType : "json",
    data : "",
    success : function(arg) {
      console_show_response_result(arg);
      decode_thread_status_package(arg);
      log.error = arg.error;
      log.content = arg.message;
      frontend_log_show(title, log);
    },
    error : function(arg) {
      log.error = "Error: ";
      log.content = "Unknown: ";
      frontend_log_show(title, log);
      console.log(arg)
    }
  })
}

/* --------------------------------------------------------------------
                            Setting
-------------------------------------------------------------------- */
function http_server_add_prefix_to_thread_network() {
  var root = $("#network_setting").serializeJson();
  var log = {error : 0, content : ""};
  var title = "Add Prefix";
  if (root.hasOwnProperty("defaultRoute") && root.defaultRoute == "on")
    root.defaultRoute = 1;
  else
    root.defaultRoute = 0;

  $.ajax({
    url : '/add_prefix',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'POST',
    dataType : "json",
    data : JSON.stringify(root),
    success : function(arg) {
      console_show_response_result(arg);
      log.error = arg.error;
      log.content = arg.message;
      frontend_log_show(title, log);
    },
    error : function(arg) {
      log.error = "Error: ";
      log.content = "Unknown: ";
      frontend_log_show(title, log);
      console.log(arg)
    }
  })
}

function http_server_delete_prefix_from_thread_network() {
  var root = $("#network_setting").serializeJson();
  var log = {error : 0, content : ""};
  var title = "Delete Prefix";
  $.ajax({
    url : '/delete_prefix',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'POST',
    dataType : "json",
    data : JSON.stringify(root),
    success : function(arg) {
      console_show_response_result(arg);
      log.error = arg.error;
      log.content = arg.message;
      frontend_log_show(title, log);
    },
    error : function(arg) {
      log.error = "Error: ";
      log.content = "Unknown: ";
      frontend_log_show(title, log);
      console.log(arg)
    }
  })
}

/* --------------------------------------------------------------------
                            commission
-------------------------------------------------------------------- */
function http_server_thread_network_commissioner() {
  var root = {
    pskd : "1234567890",
  };

  $.ajax({
    url : '/commission',
    async : true,
    contentType : 'application/json;charset=utf-8',
    type : 'POST',
    dataType : "json",
    data : JSON.stringify(root),
    success : function(arg) { console_show_response_result(arg); },
    error : function(arg) { console.log(arg) }
  })
}

/* --------------------------------------------------------------------
                            Topology
-------------------------------------------------------------------- */
function ctrl_thread_network_topology(arg) {
  var node_info = undefined;
  var topology_info = undefined;
  if (arg == "Running" || arg == "Suspend") {

    $.ajax({
      url : '/node_information',
      async : true,
      contentType : 'application/json;charset=utf-8',
      type : 'GET',
      dataType : "json",
      data : "",
      success : function(msg) {
        console_show_response_result(msg);
        node_info = msg;
        if (node_info != undefined && topology_info != undefined) {
          handle_thread_networks_topology_package(node_info, topology_info);
        }
      },
      error : function(msg) { console.log(msg) }
    })
    $.ajax({
      url : '/topology',
      async : true,
      contentType : 'application/json;charset=utf-8',
      type : 'GET',
      dataType : "json",
      data : "",
      success : function(msg) {
        console_show_response_result(msg);
        topology_info = msg;
        if (node_info != undefined && topology_info != undefined) {
          handle_thread_networks_topology_package(node_info, topology_info);
        }
      },
      error : function(msg) { console.log(msg) }
    })
  }
}

function http_server_build_thread_network_topology(arg) {
  ctrl_thread_network_topology("Running");
  document.getElementById("btn_topology").innerHTML = "Reload Topology";
}

function intToHexString(num, len) {
  var value;
  value = num.toString(16);

  while (value.length < len) {
    value = '0' + value;
  }
  return value;
} class Topology_Graph {
  constructor() {
    this.graph_isReady = false;
    this.graph_info = {'nodes' : [], 'links' : []};
    this.node_detialInfo = 'Unknown';
    this.router_number = 0;
    this.detailList = {
      'ExtAddress' : {'title' : false, 'content' : true},
      'Rloc16' : {'title' : false, 'content' : true},
      'Mode' : {'title' : false, 'content' : false},
      'Connectivity' : {'title' : false, 'content' : false},
      'Route' : {'title' : false, 'content' : false},
      'LeaderData' : {'title' : false, 'content' : false},
      'NetworkData' : {'title' : false, 'content' : true},
      'IP6Address List' : {'title' : false, 'content' : true},
      'MACCounters' : {'title' : false, 'content' : false},
      'ChildTable' : {'title' : false, 'content' : false},
      'ChannelPages' : {'title' : false, 'content' : false}
    };
  }
  update_detail_list() {
    for (var detailInfoKey in this.detailList) {
      this.detailList[detailInfoKey]['title'] = false;
    }
    for (var diagInfoKey in this.nodeDetailInfo) {
      if (diagInfoKey in this.detailList) {
        this.detailList[diagInfoKey]['title'] = true;
      }
    }
  }
}

var topology_update = new Topology_Graph();
function handle_thread_networks_topology_package(node, diag) {
  var nodeMap = {};
  var count, src, dist, rloc, child, rlocOfParent, rlocOfChild, diagOfNode,
      linkNode, childInfo;
  let topology = new Topology_Graph();

  var diag_package = diag["result"];
  for (diagOfNode of diag_package) {

    diagOfNode['RouteId'] =
        '0x' + intToHexString(diagOfNode['Rloc16'] >> 10, 2);
    diagOfNode['Rloc16'] = '0x' + intToHexString(diagOfNode['Rloc16'], 4);

    diagOfNode['LeaderData']['LeaderRouterId'] =
        '0x' + intToHexString(diagOfNode['LeaderData']['LeaderRouterId'], 2);
    for (linkNode of diagOfNode['Route']['RouteData']) {
      linkNode['RouteId'] = '0x' + intToHexString(linkNode['RouteId'], 2);
    }
  }

  count = 0;
  var node_info = node["result"];
  for (diagOfNode of diag_package) {
    if ('ChildTable' in diagOfNode) {

      rloc = parseInt(diagOfNode['Rloc16'], 16).toString(16);
      nodeMap[rloc] = count; // give id to every node

      if (diagOfNode['RouteId'] == diagOfNode['LeaderData']['LeaderRouterId']) {
        diagOfNode['Role'] = 'Leader';
      } else {
        diagOfNode['Role'] = 'Router';
      }

      topology.graph_info.nodes.push(diagOfNode);

      if (diagOfNode['Rloc16'] === node_info['Rloc16']) {
        topology.node_detialInfo = diagOfNode
      }
      count = count + 1;
    }
  }
  topology.router_number = count;
  document.getElementById("topology_netwotkname").innerHTML =
      node_info["NetworkName"];
  document.getElementById("topology_leader").innerHTML =
      "0x" + node_info["LeaderData"]["LeaderRouterId"].toString(16);
  document.getElementById("topology_router_number").innerHTML =
      count.toString();

  src = 0; // respent current router id, in order.
  for (diagOfNode of diag_package) {
    if ('ChildTable' in diagOfNode) {
      // Link bewtwen routers
      for (linkNode of diagOfNode['Route']['RouteData']) {
        rloc = (parseInt(linkNode['RouteId'], 16) << 10)
                   .toString(16); // if diagOfNode has 'ChildTable' member,
                                  // diagOfNode is router
        if (rloc in nodeMap) {
          dist = nodeMap[rloc];
          if (src < dist) {
            topology.graph_info.links.push({
              'source' : src,
              'target' : dist,
              'weight' : 1,
              'type' : 0,
              'linkInfo' : {
                'inQuality' : linkNode['LinkQualityIn'],
                'outQuality' : linkNode['LinkQualityOut']
              }
            });
          }
        }
      }

      // Link between router and child
      for (childInfo of diagOfNode['ChildTable']) {
        child = {};
        rlocOfParent = parseInt(diagOfNode['Rloc16'], 16).toString(16);
        rlocOfChild =
            (parseInt(diagOfNode['Rloc16'], 16) + childInfo['ChildId'])
                .toString(16);

        src = nodeMap[rlocOfParent];

        child['Rloc16'] = '0x' + rlocOfChild;
        child['RouteId'] = diagOfNode['RouteId'];
        nodeMap[rlocOfChild] = count;
        child['Role'] = 'Child';
        topology.graph_info.nodes.push(child);
        topology.graph_info.links.push({
          'source' : src,
          'target' : count,
          'weight' : 1,
          'type' : 1,
          'linkInfo' :
              {'Timeout' : childInfo['Timeout'], 'Mode' : childInfo['Mode']}

        });
        count = count + 1;
      }
    }
    src = src + 1;
  }

  draw_thread_topology_graph(topology);
}

var svg = d3.select('.d3graph')
              .append("svg")
              .attr('preserveAspectRatio', 'xMidYMid meet');

var force = d3.layout.force();

var link;
var node;

var trigger_flag = true;
function draw_thread_topology_graph(arg) {
  var json, tooltip;
  var scale, len;
  var topology = new Topology_Graph();
  topology = arg;
  d3.selectAll("svg > *").remove();
  scale = topology.graph_info.nodes.length;
  if (scale > 8) {
    scale = 8;
  }
  len = 150 * Math.sqrt(scale);

  // Topology graph
  svg.attr('viewBox',
           '0, 0, ' + len.toString(10) + ', ' + (len / (3 / 2)).toString(10));

  // Legend
  svg.append('circle')
      .attr('cx', len - 20)
      .attr('cy', 10)
      .attr('r', 3)
      .style('fill', "#7e77f8")
      .style('stroke', '#484e46')
      .style('stroke-width', '0.4px');

  svg.append('circle')
      .attr("cx", len - 20)
      .attr('cy', 20)
      .attr('r', 3)
      .style('fill', '#03e2dd')
      .style('stroke', '#484e46')
      .style('stroke-width', '0.4px');

  svg.append('circle')
      .attr('cx', len - 20)
      .attr('cy', 30)
      .attr('r', 3)
      .style('fill', '#aad4b0')
      .style('stroke', '#484e46')
      .style('stroke-width', '0.4px')
      .style('stroke-dasharray', '2 1');

  svg.append('circle')
      .attr('cx', len - 50)
      .attr('cy', 10)
      .attr('r', 3)
      .style('fill', '#ffffff')
      .style('stroke', '#f39191')
      .style('stroke-width', '0.4px');

  svg.append('text')
      .attr('x', len - 15)
      .attr('y', 10)
      .text('Leader')
      .style('font-size', '4px')
      .attr('alignment-baseline', 'middle');

  svg.append('text')
      .attr('x', len - 15)
      .attr('y', 20)
      .text('Router')
      .style('font-size', '4px')
      .attr('alignment-baseline', 'middle');

  svg.append('text')
      .attr('x', len - 15)
      .attr('y', 30)
      .text('Child')
      .style('font-size', '4px')
      .attr('alignment-baseline', 'middle');

  svg.append('text')
      .attr('x', len - 45)
      .attr('y', 10)
      .text('Selected')
      .style('font-size', '4px')
      .attr('alignment-baseline', 'middle');

  // Tooltip style  for each node
  tooltip = d3.select('body')
                .append('div')
                .attr('data-toggle', 'tooltip')
                .style('position', 'absolute')
                .style('z-index', '10')
                .style('font-size', '17px')
                .style('color', '#000000')
                .style('display', 'block')
                .text('a simple tooltip');

  json = topology.graph_info;

  force.distance(40)
      .size([ len, len / (3 / 2) ])
      .nodes(json.nodes)
      .links(json.links)
      .start();

  link = svg.selectAll('.link')
             .data(json.links)
             .enter()
             .append('line')
             .attr('class', 'link')
             .style('stroke', '#908484')
             // Dash line for link between child and parent
             .style('stroke-dasharray',
                    function(item) {
                      if ('Timeout' in item.linkInfo)
                        return '4 4';
                      else
                        return '0 0'
                    })
             // Line width representing link quality
             .style('stroke-width',
                    function(item) {
                      if ('inQuality' in item.linkInfo)
                        return Math.sqrt(item.linkInfo.inQuality / 2);
                      else
                        return Math.sqrt(0.5)
                    })
             // Effect of mouseover on a line
             .on('mouseover',
                 function(item) {
                   return tooltip.style('visibility', 'visible')
                       .text(item.linkInfo);
                 })
             .on('mousemove',
                 function() {
                   return tooltip.style('top', (d3.event.pageY - 10) + 'px')
                       .style('left', (d3.event.pageX + 10) + 'px');
                 })
             .on('mouseout',
                 function() { return tooltip.style('display', 'none'); });

  node = svg.selectAll('.node')
             .data(json.nodes)
             .enter()
             .append('g')
             .attr('class', function(item) { return item.Role; })
             .call(force.drag)
             // Tooltip effect of mouseover on a node
             .on('mouseover',
                 function(item) {
                   return tooltip.style('display', 'block').text(item.Rloc16);
                 })
             .on('mousemove',
                 function() {
                   return tooltip.style('top', (d3.event.pageY - 10) + 'px')
                       .style('left', (d3.event.pageX + 10) + 'px');
                 })
             .on('mouseout',
                 function() { return tooltip.style('display', 'none'); });

  d3.selectAll('.Child')
      .append('circle')
      .attr('r', '6')
      .attr('fill', '#aad4b0')
      .style('stroke', '#484e46')
      .style('stroke-dasharray', '2 1')
      .style('stroke-width', '0.5px')
      .attr('class', function(item) { return item.Rloc16; })
      .on('mouseover',
          function(item) {
            return tooltip.style('display', 'block').text(item.Rloc16);
          })
      .on('mousemove',
          function() {
            return tooltip.style('top', (d3.event.pageY - 10) + 'px')
                .style('left', (d3.event.pageX + 10) + 'px');
          })
      .on('mouseout', function() { return tooltip.style('display', 'none'); });

  d3.selectAll('.Leader')
      .append('circle')
      .attr('r', '8')
      .attr('fill', '#7e77f8')
      .style('stroke', '#484e46')
      .style('stroke-width', '1px')
      .attr('class', function(item) { return 'Stroke'; })
      // Effect that node will become bigger when mouseover
      .on('mouseover',
          function(item) {
            d3.select(this).transition().attr('r', '9');
            return tooltip.style('display', 'block').text(item.Rloc16);
          })
      .on('mousemove',
          function() {
            return tooltip.style('top', (d3.event.pageY - 10) + 'px')
                .style('left', (d3.event.pageX + 10) + 'px');
          })
      .on('mouseout',
          function() {
            d3.select(this).transition().attr('r', '8');
            return tooltip.style('display', 'none');
          })
      // Effect that node will have a yellow edge when clicked
      .on('click', function(item) {
        d3.selectAll('.Stroke')
            .style('stroke', '#484e46')
            .style('stroke-width', '1px');
        d3.select(this).style('stroke', '#f39191').style('stroke-width', '1px');
        topology.nodeDetailInfo = item;
        topology.update_detail_list();
      });

  d3.selectAll('.Router')
      .append('circle')
      .attr('r', '8')
      .style('stroke', '#484e46')
      .style('stroke-width', '1px')
      .attr('fill', '#03e2dd')
      .attr('class', 'Stroke')
      .on('mouseover',
          function(item) {
            d3.select(this).transition().attr('r', '8');
            return tooltip.style('display', 'block').text(item.Rloc16);
          })
      .on('mousemove',
          function() {
            return tooltip.style('top', (d3.event.pageY - 10) + 'px')
                .style('left', (d3.event.pageX + 10) + 'px');
          })
      .on('mouseout',
          function() {
            d3.select(this).transition().attr('r', '7');
            return tooltip.style('display', 'none');
          })
      // The same effect as Leader
      .on('click', function(item) {
        d3.selectAll('.Stroke')
            .style('stroke', '#484e46')
            .style('stroke-width', '1px');
        d3.select(this).style('stroke', '#f39191').style('stroke-width', '1px');
        topology.nodeDetailInfo = item;
        topology.update_detail_list();
      });

  if (trigger_flag) {
    force.on('tick', function() {
      link.attr('x1', function(item) { return item.source.x; })
          .attr('y1', function(item) { return item.source.y; })
          .attr('x2', function(item) { return item.target.x; })
          .attr('y2', function(item) { return item.target.y; });
      node.attr(
          'transform',
          function(
              item) { return 'translate(' + item.x + ',' + item.y + ')'; });
    });
    trigger_flag = true;
  } else {
    force.on('end', function() {
      link.attr('x1', function(item) { return item.source.x; })
          .attr('y1', function(item) { return item.source.y; })
          .attr('x2', function(item) { return item.target.x; })
          .attr('y2', function(item) { return item.target.y; });
      node.attr(
          'transform',
          function(
              item) { return 'translate(' + item.x + ',' + item.y + ')'; });
    });
  }

  topology.update_detail_list();
  topology.graph_isReady = true;
}
