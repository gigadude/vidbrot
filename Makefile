TARGET := vidbrot

LIBS := -L/usr/X11R6/lib -lglut -lGLU -lGL -lXmu -lXext -lX11 -lm
OPTS := -O6 -ffast-math -mfpmath=sse -msse2

all: $(TARGET)

%: %.cpp
	g++ -o $@ $(OPTS) $< $(LIBS)

%: %.c
	g++ -o $@ $(OPTS) $< $(LIBS)

clean: $(TARGET)
	rm $(TARGET)
