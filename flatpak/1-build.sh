#!/bin/sh

rm -rf devhelp/ repo/
flatpak-builder devhelp io.github.jkozera.ZevDocs.json || exit 1
flatpak build-export repo zevdocs
