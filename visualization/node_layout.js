//TODO: animations?
//		is it clearer to organize key blocks and node boxes/cells?
//		Resizing texts as boxes shrink
//		Replica notice rather than check the value to verify the newest value (set_cur function)

// to store the node names
var nodes = [],
// k_store and n_store hold the key and node states
	k_store = {},
	n_store = {},
// various dictionary entries
	cur='c', prv = 'p', req='r', val='v', ts='t', p_x='x', p_y='y', node='n', action='a', start_s = 's',

// globals for sizing
	c_w, c_h, col, row, top_x = [], top_y = [],
// various padding
	n_name_padding = 40, var_padding = 5, label_padding = 14,
// more sizing for the key outer boxes and cells
	kb_w, kb_h, k_c, k_r, kc_w, kc_h;

// Size function for the associative arrays ([k_|n_]store)s 
// credit: http://stackoverflow.com/questions/5223/length-of-a-javascript-object-that-is-associative-array
Object.size = function(obj) {
    var size = 0, key;
    for (key in obj) {
        if (obj.hasOwnProperty(key)) size++;
    }
    return size;
};

// Given a key, initialize the k_store entry for it, then initialize the value arrays for each node.
function init_key(k) {
	//create the data dictionary to store the state of the system
	k_store[k] = {};
	k_store[k][cur] = null;
	k_store[k][prv] = null;
	for (n in nodes) {
		n_store[n][k] = null;
	}
}

// Given an inclusive set of indices to add to the node store, initialize their dictionaries.
function init_stores(from_n, to_n) {
	for (n = from_n; n <= to_n; n++) {
		n_store[n] = {};
	}
}

// Delete all elements in the svg, clear the top x and y coordinates.
function refresh() {
	svg.selectAll('*').remove();
	top_x = [];
	top_y = [];
}

// Reset the size constants given the new length of the nodes array.
function resize_node_blocks() {
	col = 1;

	// try to make the node display as square as possible, default to wide rectangle
	while (Math.pow(col,2) < nodes.length) {
		col++;
	}
	// set the number of rows given our new column value
	row = Math.ceil(nodes.length/col);

	// set the cell width and height given the number of columns and rows
	c_w = config.SVG_WIDTH /col;
	c_h = config.SVG_HEIGHT/row;

	// set the key box width giving 5 pixels of padding on both sides
	kb_w = c_w - 2 * var_padding;	
	// set the key box height giving padding for the node name
	kb_h = c_h - get_node_label_y(0) - label_padding - 2 * var_padding;
	// initialize key cell height and width to the key box height and width
	kc_w = kb_w;
	kc_h = kb_h;
}

// Draw the container blocks including labels for the current nodes.
function draw_node_blocks() {
	i = 0;

	// right to left, top to bottom
	for (var r = 0; r < row; r++) {
		for (var c = 0; c < col; c++) {
			// only execute until we've reached the end of the nodes.
			if (i < nodes.length) {
				// draw the containing rectangle
				svg.append('rect')
					.attr('width', c_w)
					.attr('height', c_h)
					.attr('x', c_w * c)
					.attr('y', c_h * r + 1)
					.attr('stroke', 'navy')
					.attr('stroke-width', 4)
					.attr('fill', 'white');

				// insert the node name
				svg.append('text')
					.text(nodes[i])
					.attr('text-anchor', 'middle')
					.attr('x', get_node_label_x(c))
					.attr('y', get_node_label_y(r))
					.attr('font-family', 'sans-serif')
					.attr('font-size', '18');

				// underline the node name
				svg.append('line')
					.attr('x1', c_w * c + 1)
					.attr('x2', c_w * (c + 1))
					.attr('y1', get_node_label_y(r) + label_padding)
					.attr('y2', get_node_label_y(r) + label_padding)
					.attr('stroke', 'black')
					.attr('stroke-width', 2)

				// push the anchors for the key box to the top_x/y arrays
				top_x.push(c_w * c + 1 + var_padding);
				top_y.push(get_node_label_y(r) + label_padding + var_padding);

				i++;	
			}
			else {
				return;
			}
		}
	}
}

// Reset the key box and cell constants for the new size of the k_store.
function resize_key_blocks() {
	k_c = 1;
	while (Math.pow(k_c,2) < Object.size(k_store)) {
		k_c++;
	}

	k_r = Math.ceil(Object.size(k_store)/k_c);
	kc_h = kb_h/k_r;
	kc_w = kb_w/k_c;
}

// Given a key and value, save the past value of the key if there is one and set the new value to it.
function set_cur(k, v) {
	if (k in k_store) {
		// ensure that we're not burying replica messages
		// TODO: treat this differently in the logging system?
		if (k_store[k][cur] && (v != k_store[k][cur])) {
			k_store[k][prv] = k_store[k][cur];
		}
		if (v != k_store[k][cur]) {
			k_store[k][cur] = v;
		}
	}
}

// Given a key, redraw all key cells in each node for that key
function draw_key_cells_for_key(k) {
	var j=0, i=0, x, y;

	// cache the i and j values
	for (key in k_store) {
		if (key == k) {break;}

		j++;
		if (j == k_c) {
			j = 0;
			i++;
		}
	}

	for (n in nodes) {
		// erase the current key cell
		remove_key_cell(n, k);

		// get x and y anchors
		x = top_x[n], y = top_y[n];

		// use cached values to draw the rectangle
		draw_key_cell(x + kc_w * j, y + kc_h * i, n, k, get_cell_color(k, n));
	}
}

// Redraw all of the key cells for each node.
function draw_key_cells() {
	// i and j from last = j and k here.
	var x, y, j, k;

	for (n in nodes) {
		x = top_x[n];
		y = top_y[n];
		j = 0;
		k = 0;
		for (key in k_store){
			draw_key_cell(x + kc_w * j, y + kc_h * k, n, key, get_cell_color(key, n));

			j++;
			if (j == k_c) {
				j = 0;
				k++;
			}
		}
	}
}

// Given the x and y anchors, the node, the key, and the appropriate color, draw the rectangle and the text value for the key cell.
function draw_key_cell(x, y, n, k, color) {
	svg.append('rect')
		.attr('x', x)
		.attr('y', y)
		.attr('width', kc_w)
		.attr('height', kc_h)
		.attr('stroke', 'black')
		.attr('fill', color)
		.attr('stroke-width', 2)
		.attr('id', 'r'+n+k);
	svg.append('text')
		.text(k)
		.attr('x', x + kc_w / 2)
		.attr('y', y + kc_h / 2)
		.attr('text-anchor', 'middle')
		.attr('font-family', 'sans-serif')
		.attr('font-size', '18')
		.attr('fill', 'black')
		.attr('id', 't'+n+k);
}

// Remove every key cell on the svg.
function remove_all_key_cells() {
	for (n in nodes) {
		for (k in k_store) {
			remove_key_cell(n, k);
		}
	}
}

// Given a node and a key, remove the key cell
function remove_key_cell(n, k) {
	svg.select('#r'+n+k).remove();
	svg.select('#t'+n+k).remove();
}

// Given the column, calculate the x coordinate anchor for the node's label
function get_node_label_x(c) {
	return c * c_w + c_w / 2;
}

// Given the row, calculate the y coordinate anchor for the node's label
function get_node_label_y(r) {
	return r * c_h + 25;
}

// Given the key and the node, analyze the key and node stores to determine the correct value for the key cell
function get_cell_color(key, n) {
	// default lightgray for nodes who haven't seen a key yet
	var color = 'lightgray';

	if (key in n_store[n]) {
		if (n_store[n][key] == k_store[key][cur]) {
			// most current
			color = 'limegreen';
		}
		else if ((n_store[n][key] == k_store[key][prv]) && (n_store[n][key] != null)) {
			// not most
			color = 'gold';
		}
		else if (n_store[n][key] != null) {
			// blegh
			color = 'maroon';
		}
	}
	return color;
}

// Given the node, key, and value, update the display accordingly.
function update(n, k, v) {
	updateAll = false;

	// if at least 1 new node has been been introduced
	if (n >= nodes.length) {
		// initialize all new node[s]
		init_stores(nodes.length, n);

		// push new node names onto the nodes array
		for (new_n = nodes.length; new_n <= n; new_n++) {
			nodes.push('n'+new_n);
		}

		// set new node block sizes
		resize_node_blocks();
		// clear screen and keybox anchors
		refresh();
		// draw new node blocks
		draw_node_blocks();
		// set new key box sizes
		resize_key_blocks();
		// everything needs to be updated
		updateAll = true;
	}

	// check that we've seen this key before
	if (!(k in k_store)) {
		// initialize the key/node stores
		init_key(k);
		// set new key box sizes with the new number of keys
		resize_key_blocks();
		// clear all existing key cells to make new ones
		remove_all_key_cells();
		// everything needs to be updated
		updateAll = true;
	}

	// set the current state of the key if we need to
	set_cur(k, v);
	// change the node store for the current node
	n_store[n][k] = v;

	// if we need to update all cells
	if (updateAll) {
		draw_key_cells();
	}
	// or just the cells for one key
	else {
		draw_key_cells_for_key(k);
	}
}

// initialize svg element
var svg = d3.select('body').append('svg')
	.attr('width', config.SVG_WIDTH)
	.attr('height', config.SVG_HEIGHT);

// submit an XMLHttpRequest for the lgofile
var log_data = d3.csv(config.RECT_DATA_SRC, function(d, i) {
	// put all the data into shorter names, mark the step for each line
	return {
		n: +d.Node,
		k: d.Key,
		v: d.Value
	};
}, function(e,comm) {
	//visualization happens here
	var i = 0;
	// currently the update function is on an interval to simulate staggered communications
	var sint = setInterval(function() {
		update(comm[i].n, comm[i].k, comm[i].v);
		i++;
		if (i == comm.length) {
			console.log('end of communication.');
			clearInterval(sint);
		}
	}, config.TIMEOUT);
});