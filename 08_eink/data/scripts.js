      /** new chart  **/
      var yVal,
        xVal = 0;
      var updateCount = 0;
      var dataPoints = [];

      var image = new Image();
      var image_update = false;
      var grayscale;
      var grayscale_update = false;
      var grayscale_expect_width = 0;
      var grayscale_expect_height = 0;
      var qr_codes = new Array();
      function renderFunction() {
        var canvas = document.getElementById("image");
        var context = canvas.getContext("2d");
        if (!document.getElementById("video_on").checked) {
          canvas.width = 0;
          canvas.height = 0;
        } else if (image_update) {
          image_update = false;
          context.drawImage(image, 0, 0, canvas.width, canvas.height);
        } else if (grayscale_update) {
          grayscale_update = false;
          context.putImageData(grayscale, 0, 0);
        }
        requestAnimationFrame(renderFunction);
      }
      //requestAnimationFrame(renderFunction);

      image.onload = () => {
        var canvas = document.getElementById("image");
        canvas.width = image.width;
        canvas.height = image.height;
        image_update = true;
      };

      var updateChart = function () {
        updateCount++;
        chart.options.title.text = "Update " + updateCount;
        chart.render();
      };
      function b64_to_u16(b64) {
        var rawString = window.atob(b64);
        var uint8Array = new Uint8Array(rawString.length);
        for (var i = 0; i < rawString.length; i++) {
          uint8Array[i] = rawString.charCodeAt(i);
        }
        var uint16Array = new Uint16Array(uint8Array.buffer);
        return uint16Array;
      }
      function process(data) {
        document.getElementById("mem_free").innerHTML = data["mem_free"];
        document.getElementById("uptime").innerHTML = data["millis"] / 1000;
        document.getElementById("ssid").innerHTML = data["SSID"];
        document.getElementById("dbm").innerHTML = data["wifi_dBm"];
		var dig = data["digital"];
		if (dig != null) {
	      for (var i = 0;i < dig.length;i++) {
			var gpio = String(i+100);
			gpio ="GPIO" + gpio.substring(1,3);
			var e = document.getElementById(gpio);
			  if (e != null) {
				  var ch = dig.charAt(i);
				  if (ch == "1") {
					  e.innerHTML = "OUT=1";
				  }
				  else if (ch == "0") {
					  e.innerHTML = "OUT=0";
				  }
				  else if (ch == "H") {
					  e.innerHTML = "IN=1";
				  }
				  else if (ch == "L") {
					  e.innerHTML = "IN=0";
				  }
			  }
			}
		}
        return;
        document.getElementById("esp_time").innerHTML = data["time"];
        document.getElementById("sample_rate").innerHTML = data["sample_rate"];
        var ticks = 1.0 / data["sample_rate"];
        var data = b64_to_u16(data["b64"]);
        dataPoints.length = 0;
        var smooth = 2;
        for (var i = smooth; i < data.length - smooth; i++) {
          yVal = data[i];
          if (smooth == 2) {
            yVal =
              (data[i - 2] + data[i - 1] + yVal + data[i + 1] + data[i + 2]) /
              5;
          }

          xVal = i * ticks;
          dataPoints.push({
            y: yVal,
            x: xVal,
          });
        }
        updateChart();
      }
      var ws = 0;
      var last_msg = 0;
      function repeating() {
        if ("WebSocket" in window) {
          if (ws == 0 || ws.readyState >= 2) {
            // Let us open a web socket
            var url = "ws://" + location.hostname + ":81/";
            if (location.hostname == "") {
              url = "ws://esp32_08.local:81/";
            }
            document.getElementById("state").innerHTML = "connecting " + url;
            ws = new WebSocket(url);
            ws.binaryType = "arraybuffer";
            last_msg = Date.now();
            ws.onopen = function () {
              document.getElementById("state").innerHTML = "open";
              var now = Date.now();
              last_msg = now;
            };

            ws.onmessage = function (evt) {
              document.getElementById("state").innerHTML = "receiving";
              var exp_width = grayscale_expect_width;
              var exp_height = grayscale_expect_height;
              grayscale_expect_width = 0;
              grayscale_expect_height = 0;
              var now = Date.now();
              if (evt.data instanceof ArrayBuffer) {
                var data = evt.data;
                var dv = new DataView(data);
                if (
                  dv.getUint8(0) == 0xff &&
                  dv.getUint8(1) == 0xd8 &&
                  dv.getUint8(2) == 0xff
                ) {
                  // is jpeg
                  let buf = new Uint8Array(evt.data);
                  const s = buf.reduce((data, byte) => {
                    return data + String.fromCharCode(byte);
                  }, "");
                  var img_src = "data:image/jpeg;base64," + btoa(s);
                  image.src = img_src;
                  //document.getElementById("raw_image").innerText = img_src;
                  console.log("image received: " + dv.byteLength);
                  request_video();
                } else if (dv.getUint8(0) == 0 && dv.byteLength == 5) {
                  // expect next grayscale image
                  grayscale_expect_width = dv.getUint16(1);
                  grayscale_expect_height = dv.getUint16(3);
                } else if (dv.getUint8(0) == 1 && dv.byteLength == 18) {
                  // expect qr_code info
                  qr = [
                    [0, 0],
                    [0, 0],
                    [0, 0],
                    [0, 0],
                  ];
                  qr[0][0] = dv.getInt16(1, false);
                  qr[0][1] = dv.getInt16(3, false);
                  qr[1][0] = dv.getInt16(5, false);
                  qr[1][1] = dv.getInt16(7, false);
                  qr[2][0] = dv.getInt16(9, false);
                  qr[2][1] = dv.getInt16(11, false);
                  qr[3][0] = dv.getInt16(13, false);
                  qr[3][1] = dv.getInt16(15, false);
                  qr_codes[qr_codes.length] = qr;
                } else if (exp_width * exp_height == dv.byteLength) {
                  // gray scale image with width/height/pixel data
                  request_video(); // request early
                  var canvas = document.getElementById("image");
                  var width = exp_width;
                  var height = exp_height;
                  console.log(
                    "We have width: " +
                      width +
                      "px, height: " +
                      height +
                      "px, length: " +
                      dv.byteLength
                  );
                  canvas.width = width;
                  canvas.height = height;
                  context = canvas.getContext("2d");
                  grayscale = context.getImageData(0, 0, width, height);
                  var px = 0;
                  for (i = 0; i < dv.byteLength; i++) {
                    var g = dv.getUint8(i);
                    grayscale.data[px++] = g;
                    grayscale.data[px++] = g;
                    grayscale.data[px++] = g;
                    grayscale.data[px++] = 255;
                  }
                  for (i = 0; i < qr_codes.length; i++) {
                    var qr = qr_codes[i];
                    for (j = 0; j < 4; j++) {
                      for (k = j + 1; k < 4; k++) {
                        var p1_x = qr[j][0];
                        var p1_y = qr[j][1];
                        var p2_x = qr[k][0];
                        var p2_y = qr[k][1];
                        for (f = 0; f <= 50; f++) {
                          var px = (p1_x * f + p2_x * (50 - f)) / 50;
                          var py = (p1_y * f + p2_y * (50 - f)) / 50;
                          var pixel =
                            (Math.round(px) + Math.round(py) * width) * 4;
                          console.log(pixel);
                          grayscale.data[pixel++] = 0;
                          grayscale.data[pixel++] = 255;
                          grayscale.data[pixel] = 0;
                        }
                      }
                    }
                  }
                  qr_codes = new Array();
                  grayscale_update = true;
                } else {
                  console.log("unknown binary data");
                }
              } else {
                if (document.getElementById("show_raw").checked) {
                  document.getElementById("raw").innerHTML = evt.data;
                }
                process(JSON.parse(evt.data));
                var dt = now - last_msg;
                document.getElementById("debug").innerHTML = dt;
              }
              last_msg = now;
            };
            ws.onerror = function () {
              //ws.close();
            };

            ws.onclose = function () {
              // websocket is closed.
              document.getElementById("state").innerHTML = "closed";
              document.getElementById("raw").innerHTML = "";
            };
          } else {
            var dt = Date.now() - last_msg;
            if (dt > 5000) {
              ws.close();
            }
            document.getElementById("debug").innerHTML = dt;
            document.getElementById("readystate").innerHTML = ws.readyState;
          }
        } else {
          // The browser doesn't support WebSocket
          alert("WebSocket NOT supported by your Browser!");
        }
      }
      window.setInterval(repeating, 1000);
      function send_save_config() {
        if (ws != 0 && ws.readyState == 1) {
          ws.send(JSON.stringify({ save_config: true }));
        }
      }
      function send_garage() {
        if (ws != 0 && ws.readyState == 1) {
          ws.send(JSON.stringify({ garage: true }));
        }
      }
      function request_video() {
        if (ws != 0 && ws.readyState == 1) {
          if (document.getElementById("video_on").checked) {
            ws.send(JSON.stringify({ image: true }));
          }
        }
      }
      function send_value(k, v) {
        if (ws != 0 && ws.readyState == 1) {
          var j = {};
          j[k] = v;
          console.log(j);
          ws.send(JSON.stringify(j));
        }
      }
