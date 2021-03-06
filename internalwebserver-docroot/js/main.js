var cnfTree = null;
var app = new Vue({
    el: '#app',
    data: {
	local_info: {},
	local_config: {},
	ws_state: "Initialising",
	clusterstatistics: []
    },
    filters: {
    },
    methods: {
	highlightClusterNode(id) {
	    for(node of this.clusterstatistics) {
		node.highlighted = node.id == id;
	    }
	}
    },
    mounted () {
	axios
	    .get('/api/local-info.json')
	    .then(response => {
		this.local_info = response.data;
	    })
	    .catch(error => {
		console.log(error);
	    });
	axios
	    .get('/api/local-config.json')
	    .then(response => {
		this.local_config = response.data;
		cnfTree.connect();
	    })
	    .catch(error => {
		console.log(error);
	    });
    }
});

var cytoscape_main = cytoscape({
    container: document.getElementById('cytoscape_main'),
    elements: [
    ],

    layout: {
	name: 'grid',
	rows: 1
    },

    // so we can see the ids
    style: [
	{
	    selector: 'node',
	    style: {
		'label': 'data(state)',
		'text-valign': 'top',
		'text-halign': 'left'
	    }
	},
	{
	    selector: 'edge',
	    style: {
		'label': 'data(assignment)',
		'text-valign': 'top',
		'text-halign': 'center'
	    }
	}
    ]
});

var dagre_options = {
    // dagre algo options, uses default value on undefined
    nodeSep: undefined, // the separation between adjacent nodes in the same rank
    edgeSep: undefined, // the separation between adjacent edges in the same rank
    rankSep: undefined, // the separation between each rank in the layout
    rankDir: undefined, // 'TB' for top to bottom flow, 'LR' for left to right,
    ranker: undefined, // Type of algorithm to assign a rank to each node in the input graph. Possible values: 'network-simplex', 'tight-tree' or 'longest-path'
    minLen: function( edge ){ return 1; }, // number of ranks to keep between the source and target of the edge
    edgeWeight: function( edge ){ return 1; }, // higher weight edges are generally made shorter and straighter than lower weight edges

    // general layout options
    fit: true, // whether to fit to viewport
    padding: 30, // fit padding
    spacingFactor: undefined, // Applies a multiplicative factor (>0) to expand or compress the overall area that the nodes take up
    nodeDimensionsIncludeLabels: false, // whether labels should be included in determining the space used by a node
    animate: true, // whether to transition the node positions
    animateFilter: function( node, i ){ return true; }, // whether to animate specific nodes when animation is on; non-animated nodes immediately go to their final positions
    animationDuration: 500, // duration of animation in ms if enabled
    animationEasing: undefined, // easing of animation if enabled
    boundingBox: undefined, // constrain layout bounds; { x1, y1, x2, y2 } or { x1, y1, w, h }
    transform: function( node, pos ){ return pos; }, // a function that applies a transform to the final node position
    ready: function(){}, // on layoutready
    stop: function(){} // on layoutstop
};

class CNFTree {
    constructor(cy) {
	this.cy = cy;
	this.socket = null;
	let self = this;

	this.cy.on('tap', 'node', function (evt) {
	    let data = evt.target.data();
	    self.requestNodeData(data.path);
	});
	this.cy.on('mouseover', 'node', function (evt) {
	    let data = evt.target.data();
	    let id = data.remote;
	    if(id == "0") {
		id = app.local_config['id'];
	    }
	    app.highlightClusterNode(id);
	});
    }

    connect() {
	let self = this;
	let url = "ws://127.0.0.1:" + app.local_config["http-listen-port"];

	app.ws_state = "Connecting...";
	console.log("Using URL for websocket: " + url);
	this.socket = new WebSocket(url);
	this.socket.onopen = function(e) { self.onWSOpen(e); };
	this.socket.onclose = function(e) { self.onWSClose(e); };
	this.socket.onmessage = function(e) { self.onWSMessage(e); };
    }

    onWSOpen() {
	app.ws_state = "Connected!";
	this.socket.send(JSON.stringify({type: "ping"}));
    }

    onWSClose(event) {
	app.ws_state = "Disconnected!";
	alert("Websocket closed, no more requests possible.");
    }

    onWSMessage(event) {

	// Sometimes, the websocket on the server side can be overloaded and the framing
	// does not work correctly. In these cases, every incoming line is
	// split to still be able to frame JSON messages.
	let data = event.data.split('\n');

	for(let i = 0; i < data.length; ++i) {
	    if(data[i].length < 3) continue;

	    let msg = null;
	    try {
		msg = JSON.parse(data[i]);
	    } catch(e) {
		console.log("Could not parse message from WS! Message:" + e);
		console.log(event.data[i]);
		continue;
	    }

	    if(msg === null) continue;

	    try {
		this.handleWSMessage(msg);
	    } catch(e) {
		console.log("Could not process message from WS! Message: " + e);
		console.log(msg);
	    }
	}

    }

    handleWSMessage(msg) {
	switch(msg.type) {
	case "cnftree-update": {
	    this.handleCNFTreeUpdate(msg);
	    break;
	}
	case "error": {
	    alert("Error: " + msg.message);
	    break;
	}
	case "pong": {
	    break;
	}
	case "clusterstatistics": {
	    app.clusterstatistics = msg["ClusterStatistics"];
	    break;
	}
	case undefined: {
	    throw "REQUIRE .type field!";
	    break;
	}
	default: {
	    console.log("Unknown message received!");
	    console.log(msg);
	    break;
	}
	}
    }

    requestNodeData(path) {
	if(app.ws_state != "Connected!") {
	    return;
	}
	this.socket.send(JSON.stringify({type: "cnftree-request-path", path: path, next: true}));
    }

    handleCNFTreeUpdate(msg) {
	// Find root.
	let rootQuery = this.cy.$('node[id="root"]');
	let root = null;

	if(rootQuery.length == 0) {
	    // Add root node, it does not exist yet.
	    this.cy.add({ group: "nodes", data: { id: "root", path: '', remote: msg.remote }});
	    rootQuery = this.cy.$('node[id="root"]');
	}
	root = rootQuery[0];

	if(msg.path == "") {
	    // This is the root node! Apply it to there.
	    root.data.state = msg.state;
	} else {
	    let source = msg.path.substr(0, msg.path.length - 1);
	    if(source == '')
		source = 'root';

	    let right = msg.path.substr(-1) == '1';
	    let assignment = right ? '⊤' : '⊥';
	    let pos = {x: assignment ? 1000 : -1000, y: 0};

	    let node = this.cy.$("node[path=\"" + msg.path + "\"]");
	    if(node.length == 0) {
		this.cy.add({ group: "nodes", position: pos, data: {
		    id: msg.path, path: msg.path, remote: msg.remote }});
		this.cy.add({ group: "edges", data: {
		    id: msg.path + "e", source: source, target: msg.path, assignment: assignment }});
	    } else {
		node[0].data("state", msg.state);
	    }
	}
	this.cy.layout({ name: 'dagre', options: dagre_options }).run();
    }
}

cnfTree = new CNFTree(cytoscape_main);

cytoscape_main.layout({ name: 'dagre', options: dagre_options }).run();
