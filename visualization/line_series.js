//TODO: more than one value for the backtrace functionality?
//		continuation (deleting part of the field to continue moving)?
//		obligatory animation but eh

// to store the client names/keep track of current number of clients
var clients = [],
// to store the current clients processing a request
	in_req_clients = [],
// dictionary to store state
	store = {},
// various dictionary entries
	cur='c', prv = 'p', req='r', val='v', ts='t', p_x='x', p_y='y', client='c', action='a', start_s = 's';

function init_key(k) {
	//create the data dictionary to store the state of the system
	store[k] = {};
	store[k][req] = {};
	store[k][cur] = null;
	store[k][prv] = null;
}

// Given the key, value, timestamp, and x and y coordinates of the ACK point, set the current state of the key so we can draw back to the stale value.
// TODO: Currently don't utilize the timestamp value to indicate staleness. p_x/y values are a possible alternate.
function set_cur(k, v, ts, x, y) {
	if (k in store) {
		// check that there is even a current state created yet
		if (store[k][cur]) {
			if (!store[k][prv]) {
				// create the previous dictionary if necessary
				store[k][prv] = {};
			}

			// move all the stale data into the previous state
			store[k][prv][val] = store[k][cur][val];
			store[k][prv][ts] = store[k][cur][ts];
			store[k][prv][p_x] = store[k][cur][p_x];
			store[k][prv][p_y] = store[k][cur][p_y];
		}
		else {
			// create the current dictionary if necessary
			store[k][cur] = {};
		}

		// save the current state
		store[k][cur][val] = v;
		store[k][cur][ts] = ts;
		store[k][cur][p_x] = x;
		store[k][cur][p_y] = y;
	}
	else {
		// initialize the key if it isn't in the store already, recall the function with key initialized
		init_key(k);
		set_cur(k, v, ts, x, y);
	}
}

// Given the key, client, request, (possible value), and current step, begin the request and draw the initial points.
function start_req(k, c, r, v, s) {
	if (k in store) {
		// create the request dictionary for the given client
		store[k][req][c] = {};
		// store the request data in the dictionary
		store[k][req][c][client] = c;
		store[k][req][c][req] = r;
		store[k][req][c][val] = v;
		store[k][req][c][start_s] = s;

		// draw the start point to begin the request (thick line is handled by draw_lines())
		draw_start_point(get_point_x(s), get_point_y(c));

		// create the label for the beginning point
		var label = r+'\n'+k;
		if (r == 'put') {
			label += '='+v;
		}

		// draw the label
		draw_label(s, c, label);
	}
	else {
		// initialize the key if it isn't in the store already, recall the function with key initialized
		init_key(k);
		start_req(k, c, r, v, s);
	}
}

// Given the key, client, request type, value, and current step, end the outstanding request.
function end_req(k, c, t, v, s) {
	if (k in store) {
		if (c in store[k][req]) {
			//draw request in appropriate color
			draw_request(store[k][req][c], k, t, v, s);

			//remove outstanding request from store
			delete store[k][req][c];
		}
		else {
			console.log('end_request called with client value never inserted into outstanding requests.')
		}
	}	
	else {
		console.log('end_request called on non-existing key.')
	}
}

function draw_get_origin(c, s, x1, y1) {
	svg.append('line')
		.attr('x1', x1)
		.attr('y1', y1)
		.attr('x2', get_line_x1(s))
		.attr('y2', get_line_y(c))
		.attr('stroke', 'gray')
		.attr('stroke-width', 2)
}

// Given an outstanding request dictionary, key, request type, value, and current step, draw the completed request as updated/stale/old.
function draw_request(r, k, t, v, s) {
	var color = 'black';
	var rsp = 'ACK';

	// if get request, decide update state
	if (r[req] == 'get') {
		if (!(v == store[k][cur][val])) {
			// not the most recent
			if (v == store[k][prv][val]) {
				// but the second most
				// TODO: check p_x/y here as well? No timestamp integration at the moment
				color = 'gold';
				draw_get_origin(r[client], s, store[k][prv][p_x], store[k][prv][p_y]);
			}
			else {
				//or not at all
				color = 'maroon';
			}
		}
		else {
			// the most recent!
			color = 'limegreen';
		}
		// generate the response
		rsp = k+'='+v;
	}
	
	// remove old outstanding request lines
	remove_request_line(r[start_s], s, r[client]);

	// draw new (maybe colorful) line
	svg.append('line')
		.attr('x1', get_line_x1(r[start_s]))
		.attr('y1', get_line_y(r[client]))
		.attr('x2', get_line_x1(s))
		.attr('y2', get_line_y(r[client]))
		.attr('stroke', color)
		.attr('stroke-width', '4');

	// draw response label
	draw_label(s, r[client], rsp);

	//remove black starting point
	remove_start_point(get_point_x(r[start_s]), get_point_y(r[client]));

	//draw new (maybe colorful) points
	draw_point(get_point_x(r[start_s]), get_point_y(r[client]), color);
	draw_point(get_point_x(s), get_point_y(r[client]), color);
}

// Given the current step, draw all client lines, thick if in an outstanding request and thin otherwise
function draw_lines(step) {
	for (var i = 0; i < clients.length; i++) {
		// 2 = thin stroke width
		var sw = 2;
		// don't need an ID to remove thin lines, just thick
		var id = null;

		if (in_req_clients.indexOf(i) >= 0) {
			// 4 = thick
			sw = 4;
			// beginning of the id
			id = 'l';
		}
		
		var l = svg.append('line')
			.attr('x1', get_line_x1(step))
			.attr('y1', get_line_y(i))
			.attr('x2', get_line_x2(step))
			.attr('y2', get_line_y(i))
		    .attr('stroke-width', sw)
		    .attr('stroke', 'black');
		if (id != null) {
			// separate the numbers to create a unique ID - lines will never share y values unless in the same request; and the same request will never share an x1 value
			l.attr('id', id+get_line_x1(step)+id+get_line_y(i));
		}
	}
}

// Draw all of the currently known clients of the system.
function draw_clients() {
	for (c in clients){
		svg.append('text')
			.text(clients[c])
			.attr('y', 100 + c * 75)
			.attr('x', '25')
			.attr('font-family', 'sans-serif')
			.attr('font-size', '18')
			.attr('id', clients[c]);		
	}
}

// Remove all of the existing client text boxes.
function remove_clients() {
	for (c in clients) {
		svg.select('#'+clients[c]).remove();
	}
}

// Given x and y coordinates and a color, draw a circle of that color at that point with radius 3.
function draw_point(x, y, color) {
	svg.append('circle')
		.attr('cx', x)
		.attr('cy', y)
		.attr('r', 3)
		.attr('stroke', color)
		.attr('fill', color);
}

// Given x and y coordinates and a color, draw a circle of that color at that point with radius 3, with a unique identifier to make the circle addressable in the future.
function draw_start_point(x, y) {
	svg.append('circle')
		.attr('cx', x)
		.attr('cy', y)
		.attr('r', 3)
		.attr('stroke', 'black')
		.attr('fill', 'black')
		.attr('id', 'p'+x+'black'+y);
}

// Given x and y coordinates, remove the starting unique point.
function remove_start_point(x, y) {
	svg.select('#'+'p'+x+'black'+y).remove();
}

// Given two steps, remove all thick lines between the two steps.
function remove_request_line(from_s, to_s, c) {
	for (var s = from_s; s < to_s; s++) {
		svg.select('#'+'l'+get_line_x1(s)+'l'+get_line_y(c)).remove();
	}
}

// Given the current step and the timestamp of the log, draw the timestamp above the client field.
function draw_timestamp(step, t) {
	svg.append('text')
		.text(t)
		.attr('x', get_text_x(step))
		.attr('y', 55)
		.attr('font-family', 'sans-serif')
		.attr('font-size', '18')
		.attr('text-anchor', 'middle');
}

// Given the current step, the client number, and the label, write it to the screen; splitting newlines if necessary.
function draw_label(step, c, label) {
	var t = svg.append('text')
		.attr('x', get_text_x(step))
		.attr('y', get_text_y(c))
		.attr('font-family', 'sans-serif')
		.attr('font-size', '18')
		.attr('id', 't'+get_text_x(step)+'label'+get_text_y(c))
		.attr('text-anchor', 'middle');
	for (i in label.split('\n')) {
		t.append('tspan')
			.text(label.split('\n')[i])
			.attr('dy', i * 20)
			.attr('x', get_text_x(step));
	}
}

// Given the current step, calculate the appropriate x value for text.
function get_text_x(step) {
	return 75 + step * 50;
}

// Given the client number, calculate the appropriate y value for labels.
function get_text_y(c) {
	return 119 + 75 * c;
}

// Given the current step, calculate the appropriate beginning x value for a step line.
function get_line_x1(step) {
	return 75 + step * 50;
}

// Given the current step, calculate the appropriate ending x value for a step line.
function get_line_x2(step) {
	return get_line_x1(step + 1);
}

// Given the client number, calculate the appropriate y value for a step line.
function get_line_y(c) {
	return 94 + c * 75;
}

// Given the current step, calculate the appropriate x value for a step point.
function get_point_x(step) {
	return 73 + step * 50;
}

// Given the client number, calculate the appropriate y value for a step point.
function get_point_y(c) {
	return 94 + c * 75;
}

// Given an inclusive range of new clients and the current step, draw the appropriate thin lines up to the step.
function update_lines(from_c, to_c, s) {
	for (var c = from_c; c <= to_c; c++) {
		svg.append('line')
			.attr('x1', get_line_x1(0))
			.attr('y1', get_line_y(c))
			.attr('x2', get_line_x2(s))
			.attr('y2', get_line_y(c))
		    .attr('stroke-width', 2)
		    .attr('stroke', 'black');
	}
}

// Given the new line of data, process the request appropriately.
function update(data) {
	// stupidity check
	if (data.c == NaN) {
		console.log('client id conversion failure, skipping this communication.')
		return;
	}

	// check that the client list is currently exhaustive
	if (data.c >= clients.length || clients.length == 0) {
		// if not, remove the current client text fields
		remove_clients();

		// update new clients with leading lines
		update_lines(clients.length, data.c, data.s);

		// create new entries in the clients array for the new clients
		for (c = clients.length; c <= data.c; c++) {
			clients.push('C'+c);
		}

		// update the screen with all of the client monikers.
		draw_clients();
	}
	
	// draw the timestamp mark of the data received
	draw_timestamp(data.s, data.t);

	if (data.a == 'req') {
		// push the client into the in_req_clients array to mark it in an outstanding request
		in_req_clients.push(data.c);

		// draw the next step of lines
		draw_lines(data.s);

		// start the request that the communication indicates
		start_req(data.k, data.c, data.r, data.v, data.s);
	}
	else {
		// end the given request, taking the client out of the outstanding request array
		if (in_req_clients.indexOf(data.c) >= 0) {
			delete in_req_clients[in_req_clients.indexOf(data.c)];
		}

		// draw the next step of lines
		draw_lines(data.s);

		// end communication and draw new colored lines (should be called after draw_lines)
		end_req(data.k, data.c, data.r, data.v, data.s);

		if (data.a == 'resp' && data.r == 'put') {
			// if the completed the request changed the state of the data store, mark that change in our store
			set_cur(data.k, data.v, data.t, get_point_x(data.s), get_point_y(data.c));
		}
	}

	console.log('processed request.');
}

// TODO: resizing as communications reach the right hand side of the svg
// initialize the SVG tag
var svg = d3.select('body').append('svg')
	.attr('width', config.SVG_WIDTH)
	.attr('height', config.SVG_HEIGHT);

// draw the 0 timestamp, mark milliseconds
draw_timestamp(0, "0ms");

// submit an XMLHttpRequest to get the logfile
var log_data = d3.csv(config.LINE_DATA_SRC, function(d, i) {
	// put all the data into shorter names, mark the step for each line
	return {
		c: +d.Client,
		r: d.Request,
		k: d.Key,
		t: d.Timestamp,
		a: d.Action,
		v: d.Value,
		s: i+1
	};
}, function(e,comm) {
	// visualization happens here
	var i = 0;
	// currently the update function is on an interval to simulate staggered communications
	var sint = setInterval(function() {
		update(comm[i]);
		i++;
		if (i == comm.length) {
			console.log('end of communication.');
			clearInterval(sint);
		}
	}, config.TIMEOUT);
});
