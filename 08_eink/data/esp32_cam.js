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
	<tr> <td> 5V             </td><td>        </td><td            ></td><td            ></td><td>        </td><td> 3.3V     </td> </tr>
	<tr> <td> GND            </td><td>        </td><td            ></td><td id="GPIO16"></td><td> GPIO16 </td><td> U2RXD    </td> </tr>
	<tr> <td> HS2_DATA2      </td><td> GPIO12 </td><td id="GPIO12"></td><td id="GPIO00"></td><td> GPIO00 </td><td> /BOOT, CSI_MCLK </td> </tr>
	<tr> <td> HS2_DATA3      </td><td> GPIO13 </td><td id="GPIO13"></td><td            ></td><td>        </td><td> GND      </td> </tr>
	<tr> <td> HS2_CMD        </td><td> GPIO15 </td><td id="GPIO15"></td><td            ></td><td>        </td><td> 3.3V/5V  </td> </tr>
	<tr> <td> HS2_CLK        </td><td> GPIO14 </td><td id="GPIO14"></td><td id="GPIO03"></td><td> GPIO03 </td><td> U0RXD    </td> </tr>
	<tr> <td> HS2_DATA0      </td><td> GPIO02 </td><td id="GPIO02"></td><td id="GPIO01"></td><td> GPIO01 </td><td> U0TXD    </td> </tr>
	<tr> <td> HS2_DATA1/FLASH</td><td> GPIO04 </td><td id="GPIO04"></td><td            ></td><td>        </td><td> GND      </td> </tr>
	<tr style="background: #aaa;"> <td colspan="7"></td> </tr>	
	<tr> <td> LED            </td><td> GPIO33 </td><td id="GPIO33"></td><td            ></td><td>        </td><td>             </td> </tr>
	</tbody>
	</table>`;

