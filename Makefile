fmt: node_modules/prettier/package.json links
	clang-format --style=Google -i `find */src */include -name '*.h' -type f`
	clang-format --style=Google -i `find */src */include -name '*.c' -type f`
	clang-format --style=Google -i `find */src */include -name '*.cpp' -type f`
	npx prettier --write */data/*.html
	find . -name wifi_secrets.cpp -delete

links:
	for i in [0-9]*; do (cd $$i;mkdir -p tools include src data);done
	for i in [0-9]*; do (cd $$i;echo $$i; ln -sf ../prj_template/min_spiffs.csv .);done
	for i in [0-9]*/tools; do (cd $$i;echo $$i; ln -sf ../../prj_template/tools/* .);done
	for i in [0-9]*/include; do (cd $$i;echo $$i; ln -sf ../../prj_template/include/* .);done
	for i in [0-9]*/data; do (cd $$i;echo $$i; ln -sf ../../prj_template/data/*.html .);done
	for i in [0-9]*/src; do (cd $$i;echo $$i; ln -sf ../../prj_template/src/*.cpp .);done
	for i in [0-9]*/src; do (cd $$i;echo $$i; ln -sf ../../../.private/private_wifi_secrets.cpp wifi_secrets.cpp);done
	# Generate empty version.h if not present in project include
	for i in [0-9]*; do \
		if [ ! -f "$$i/include/version.h" ]; then \
			echo "Creating empty version.h for $$i"; \
			echo '// Auto-generated placeholder' > "$$i/include/version.h"; \
			echo '#ifndef VERSION_H' >> "$$i/include/version.h"; \
			echo '#define VERSION_H' >> "$$i/include/version.h"; \
			echo '#define GIT_HASH "0000000"' >> "$$i/include/version.h"; \
			echo '#define GIT_BRANCH "unknown"' >> "$$i/include/version.h"; \
			echo '#define BUILD_TIME "1970-01-01 00:00:00"' >> "$$i/include/version.h"; \
			echo '#define VERSION_STRING "unknown-0000000"' >> "$$i/include/version.h"; \
			echo '#endif' >> "$$i/include/version.h"; \
		fi \
	done
	# Update .gitignore
	mv .gitignore .gitignore2
	gawk 'skip==0{print} /##===/{skip=1}' .gitignore2 >.gitignore
	find * -type l | sort >>.gitignore
	rm .gitignore2

build: links
	for i in [0-9]*;do (echo "Process $$i";cd $$i;rm -fR .pio;pio run);done

node_modules/prettier/package.json:
	npm install --save-dev --save-exact prettier
