all: icos.ps dodec.ps dodec.pdf

dodec.pdf: dodec.ps
	ps2pdf dodec.ps

dodec.ps: pic.ps desc
	$$HOME/src/polyhedra/drawnet.py \
		-sSTHPD -aKLHTF,ERGLK -aBSDJI,IJCQA \
		-aQREMA,IJCQA -aCOGRQ,ERGLK \
		-ppic.ps dodec | \
	sed > dodec.ps \
		-e 's/288 400 translate/288 430 translate/' \
		-e 's/[0-9\.]* dup scale/70 dup scale/' \
		-e 's/[0-9\.]* setlinewidth/0.002 setlinewidth/'

icos.ps: pic.ps desc
	$$HOME/src/polyhedra/drawnet.py \
		-sEGB -aIFC,IKF -aFHC,IFC -aDHF,FHC \
		-ppic.ps icos icos.ps

desc: makedesc.py
	python2.1 ./makedesc.py > desc

-include desc.d

pic.ps: map.ps desc
	perl -ne 'if (/\((.*)\) \(r\) file cvx exec/)' \
	      -e '{ system "cat $$1" } else { print }' map.ps > pic.ps

icon: badgersmall.jpeg badgerlarge.jpeg

badgersmall.jpeg: badger.png
	convert -scale 150x150 $< $@
badgerlarge.jpeg: badger.png
	convert -scale 450x450 $< $@

badger.png: badger.pov badger-frag.pov
	povray +fn +I$< +O$@ +W900 +H900 +V -D +X

badger-frag.pov: pic.ps desc dodec
	$$HOME/src/polyhedra/povpoly.py -ppic.ps dodec $@

clean:
	rm -f desc pic.ps dodec.ps icos.ps desc.d *-frag.pov* Memory.Log
	rm -f badger.png

spotless: clean
	rm -f *.pdf *.png *.jpeg
