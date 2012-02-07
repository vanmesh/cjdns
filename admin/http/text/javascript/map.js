var w = 960;
var h = 600;
var fill = d3.scale.category20()
var DEBUGGING = false;

/** True if we don't want to map nodes with 0 reach because they might not actually be reachable. */
var EXCLUDE_ZERO_NODES = false;

var vis = d3.select("#viz").append("svg").attr("width", w).attr("height", h);

var json = {
  links: [],
  nodes: []
};

var nodes = {};

var routes;
var pairs;

var doRequest = function(url, callback) {
    var xmlhttp = new XMLHttpRequest();
    xmlhttp.open("GET", url, true);
    xmlhttp.onreadystatechange = function() {
        if (xmlhttp.readyState==4) {
           callback(bdecode(xmlhttp.responseText));
        }
    };
    xmlhttp.send(null);
};

var getData = function() {
    // return true if a runs through b
    var runsThrough = function(a, b)
    {
        var mask = (1 << parseInt(Math.log(b))) - 1;
        return (a & mask) == (b & mask);
    };

    var buildBranch = function(parsedRoutes, indexOfTip)
    {
        var branch = new Array();
        branch.push(parsedRoutes[indexOfTip]);
        var path = parsedRoutes[indexOfTip].path;
        var lastIp = parsedRoutes[indexOfTip].name;
        for (var i = indexOfTip + 1; i < parsedRoutes.length; i++) {
            if (runsThrough(path, parsedRoutes[i].path)) {
                branch.push(parsedRoutes[i]);
                path = parsedRoutes[i].path;
                lastIp = parsedRoutes[i].name;
            }
        }
        return branch;
    }

    var buildTree = function(parsedRoutes)
    {
        var tree = new Array();
        for (var i = 0; i < parsedRoutes.length; i++) {
            tree.push({
                value: parsedRoutes[i].link,
                branch: buildBranch(parsedRoutes, i)
            });
        }
        return tree;
    };

    var parseRoutes = function(list)
    {
         var out = [];
         for (var i = 0; i < list.length; i++) {
             if (EXCLUDE_ZERO_NODES && list[i].link == 0) {
                 continue;
             }
             out.push({
                 name: list[i].ip,
                 path: new Number('0x' + list[i].path.replace(/\./g, '')),
                 link: list[i].link 
             });
         }
         out.sort(function(a, b) { return b.path - a.path });
         return out;
    }

    var getPairs = function(tree)
    {
        var pairs = {};
        for (var i = 0; i < tree.length; i++) {
            var branch = tree[i];
            for (var j = 0; j < branch.branch.length - 1; j++) {
                var ips = [branch.branch[j].name, branch.branch[j + 1].name];
                if (ips[0] == ips[1]) {
                    continue;
                }
                ips.sort();
                var p1 = pairs[ips[0]];
                if (!p1) {
                    p1 = pairs[ips[0]] = {};
                }
                if (!p1[ips[1]]) {
                    p1[ips[1]] = new Number(branch.value);
                } else {
                    p1[ips[1]] += new Number(branch.value);
                }
            }
        }
        return pairs;
    };

    doRequest('/api/?q=d1:q19:NodeStore_dumpTablee', function(responseJSON) {
        var table = responseJSON.routingTable;
        table.sort(function(a, b){ return a.link - b.link; });
        document.getElementById('nodes-list').innerText = JSON.stringify(table);
        routes = parseRoutes(table);
        var tree = buildTree(routes);
        pairs = getPairs(tree);

        doLayout(1);
    });
};

var authedRequest = function(request, password, cookie)
{
    var hashA = SHA256_hash('' + password + cookie);
    request.cookie = cookie;
    request.hash = hashA;
    request.aq = request.q;
    request.q = 'auth';
    var reqStr = bencode(request);
    var hashB = SHA256_hash(reqStr);
    request.hash = hashB
    return bencode(request);
};

var setupSubmitEvent = function() {
    document.removeEventListener('DOMContentLoaded', setupSubmitEvent, false);
    document.getElementById('runcmd-submit').addEventListener('mousedown', function(ev) {
        ev.stopPropagation();
        var cmdInput = document.getElementById('runcmd-cmd').value;
        var cmdPasswd = document.getElementById('runcmd-passwd').value;
        var output = document.getElementById('runcmd-output');
        output.innerText = '';
        var query = null;
        try {
            query = JSON.parse(cmdInput);
        } catch (e) {
            output.innerText = "failed to parse input\n\n" + JSON.stringify(e);
            return;
        }
        doRequest('/api/?q=d1:q6:cookiee', function(responseJSON) {
            var authQuery = authedRequest(query, cmdPasswd, responseJSON.cookie);
            doRequest('/api/?q=' + authQuery, function(response) {
                output.innerText = JSON.stringify(response);
            });
        });
    }, false);
};
document.addEventListener("DOMContentLoaded", setupSubmitEvent, false);



var doLayout = function(step)
{
    if (!step) {
        getData();
        return;
    }

    var nodeByIp = {};
    for (var i = 0; i < routes.length; i++) {
        nodeByIp[routes[i].name] = routes[i];
    }

    if (DEBUGGING) {
        var noLinks = {};
    }
    for (var ip in nodeByIp) {
        var node = nodeByIp[ip];
        json.nodes.push(node);
        if (DEBUGGING) {
            noLinks[node] = 1;
        }
        for (var tar in pairs[node.name]) {
            json.links.push({
                source: node,
                target: nodeByIp[tar],
                distance: 200 / (pairs[node.name][tar] + 1)
            });
            if (DEBUGGING) {
                delete noLinks[node];
                delete noLinks[nodeByIp[tar]];
            }
        }
    }

    if (DEBUGGING) {
        for (var nl in noLinks) {
            console.log(nl.name + ' has no links!');
        }
    }

    var force = d3.layout.force()
        .charge(-300)
        .linkDistance(function(link){return link.distance})
        .nodes(json.nodes)
        .links(json.links)
        .size([w, h])
        .start();


    var link = vis.selectAll("line.link").data(json.links).enter().append("line").attr("class", "link").style("stroke-width", function(d) {
      return Math.sqrt(d.value);
    }).attr("x1", function(d) {
      return d.source.x;
    }).attr("y1", function(d) {
      return d.source.y;
    }).attr("x2", function(d) {
      return d.target.x;
    }).attr("y2", function(d) {
      return d.target.y;
    });

    var node = vis.selectAll("circle.node").data(json.nodes).enter().append("circle").attr("class", "node").attr("cx", function(d) {
      return d.x;
    }).attr("cy", function(d) {
      return d.y;
    }).attr("r", 5).style("fill", function(d) {
      return fill(d.group);
    }).call(force.drag);

    node.append("title").text(function(d) {
      return d.name;
    });

    force.on("tick", function() {
      link.attr("x1", function(d) {
        return d.source.x;
      }).attr("y1", function(d) {
        return d.source.y;
      }).attr("x2", function(d) {
        return d.target.x;
      }).attr("y2", function(d) {
        return d.target.y;
      });

      node.attr("cx", function(d) {
        return d.x;
      }).attr("cy", function(d) {
        return d.y;
      });
    });
  }
