fmt: node_modules/prettier/package.json links
	npx prettier --write */src/*.html
	clang-format --style=Google -i `find */src */include -name '*.h' -type f`
	clang-format --style=Google -i `find */src */include -name '*.c' -type f`
	clang-format --style=Google -i `find */src */include -name '*.cpp' -type f`
	find . -name wifi_secrets.cpp -delete

links:
	for i in [0-9]*; do (cd $$i;mkdir -p tools include src data);done
	for i in [0-9]*; do (cd $$i;echo $$i; ln -sf ../prj_template/min_spiffs.csv .);done
	for i in [0-9]*/tools; do (cd $$i;echo $$i; ln -sf ../../prj_template/tools/* .);done
	for i in [0-9]*/include; do (cd $$i;echo $$i; ln -sf ../../prj_template/include/* .);done
	for i in [0-9]*/data; do (cd $$i;echo $$i; ln -sf ../../prj_template/data/*.html .);done
	for i in [0-9]*/src; do (cd $$i;echo $$i; ln -sf ../../prj_template/src/*.cpp .);done
	for i in [0-9]*/src; do (cd $$i;echo $$i; ln -sf ../../private_wifi_secrets.cpp wifi_secrets.cpp);done
	# Update .gitignore
	mv .gitignore .gitignore2
	gawk 'skip==0{print} /##===/{skip=1}' .gitignore2 >.gitignore
	find * -type l | sort >>.gitignore
	rm .gitignore2

build: links
	for i in [0-9]*;do (echo "Process $$i";cd $$i;rm -fR .pio;pio run);done

node_modules/prettier/package.json:
	npm install --save-dev --save-exact prettier
