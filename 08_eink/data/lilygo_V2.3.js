function selectCell(event) {
	if (event.target instanceof HTMLTableCellElement) {
		var t = event.target.id;
		if (t.match("GPIO[0-9][0-9]") != null) {
			var x = t.replace(/GPIO/,"");
			var gpio = parseInt(x, 10);
			var e = event.target;
			if (e.innerHTML == "OUT=0") {
				send_value("pin_to_high",gpio);
			}
			else if (e.innerHTML == "OUT=1") {
				send_value("pin_to_low",gpio);
			}
		}
		else {
			t = event.target.textContent.trim(" ");
			if (t.match("GPIO[0-9][0-9]") != null) {
				var x = t.replace(/GPIO/,"");
				var gpio = parseInt(x, 10);
				var e = document.getElementById(t);
				if (e != null) {
					if (e.innerHTML.match("IN") == null) {
						send_value("as_input",gpio);
					}
					else {
						send_value("pin_to_high",gpio);
					}
				}
			}
		}
	}
}
var board_definition = `
	<table class="table table-striped table-bordered" onclick="selectCell(event);">
	<tr>
	<thead>
	<th> </th> <th> GPIO </th> <th> </th> <th> </th> <th> </th> <th> GPIO </th> <th> </th>
	</tr>
	</thead>
	<tbody>
	<tr> <td>                </td><td>        </td><td></td> <td rowspan="14">&nbsp;</td> <td > </td> <td> </td><td> </td> </tr>
	<tr> <td> 3V3            </td><td>        </td><td            ></td><td id="GPIO36"></td><td> GPIO36</td><td> FSVP/ADC1.0 </td> </tr>
	<tr> <td> GND            </td><td>        </td><td            ></td><td id="GPIO39"></td><td class="success"> GPIO39</td><td> FSVN/ADC1.3 </td> </tr>
	<tr> <td> VSPIHD/SDA     </td><td> GPIO21 </td><td id="GPIO21"></td><td            ></td><td>        </td><td> RST </td> </tr>
	<tr> <td> U0TXD/CLK3/TXD </td><td> GPIO01 </td><td id="GPIO01"></td><td id="GPIO34"></td><td> GPIO34 </td><td> ACC1.6 </td> </tr>
	<tr> <td> U0RXD/CLK2/RXD </td><td> GPIO03 </td><td id="GPIO03"></td><td            ></td><td>        </td><td> 3V3 </td> </tr>
	<tr> <td> VSPIWP/SCL     </td><td> GPIO22 </td><td id="GPIO22"></td><td id="GPIO32"></td><td> GPIO32 </td><td> 32K_XP/ADC1.4 </td> </tr>
	<tr> <td> U0CTS/VSPIO/LED</td><td> GPIO19 </td><td id="GPIO19"></td><td id="GPIO33"></td><td> GPIO33 </td><td> 32K_XN/ADC1.5 </td> </tr>
	<tr> <td> TOUCH6/ADC2.6  </td><td> GPIO14 </td><td id="GPIO14"></td><td id="GPIO25"></td><td> GPIO25 </td><td> ADC2.8/DAC2 </td> </tr>
	<tr> <td> HSPIWP/ADC2.2  </td><td> GPIO02 </td><td id="GPIO02"></td><td id="GPIO26"></td><td> GPIO26 </td><td> ADC2.9/DAC1 </td> </tr>
	<tr> <td> HSPICS0/ADC2.3 </td><td> GPIO15 </td><td id="GPIO15"></td><td id="GPIO27"></td><td> GPIO27 </td><td> ADC2.7/TOUCH7 </td> </tr>
	<tr> <td> TOUCH4/ADC2.4  </td><td> GPIO13 </td><td id="GPIO13"></td><td id="GPIO12"></td><td> GPIO12 </td><td> ADC2.5/TOUCH5 </td> </tr>
	<tr> <td> 5V             </td><td>        </td><td            ></td><td id="GPIO00"></td><td> GPIO00 </td><td> CLK1/ADC2.3/TOUCH1 </td> </tr>
	<tr> <td> VBAT          </td><td>        </td><td            ></td><td            ></td><td>        </td><td> GND </td> </tr>
	</tbody>
	</table>`;

