let processor = {
  timerCallback: function() {
    if (this.video.paused || this.video.ended) {
      return;
    }
    this.computeFrame();
    let self = this;
    setTimeout(function () {
        self.timerCallback();
      }, 50);
  },

  doLoad: function() {
    this.width = 160;
    this.height = 128;
    this.ws = new WebSocket("ws://192.168.4.1/video");
    this.ws.binaryType = 'arraybuffer';
    this.bytearray = new Uint8Array(40960);
    this.video = document.getElementById("video");
    this.c1 = document.getElementById("c1");
    this.ctx1 = this.c1.getContext("2d");
    this.c2 = document.getElementById("c2");
    this.ctx2 = this.c2.getContext("2d");
    let self = this;
    this.video.addEventListener("play", function() {
        self.timerCallback();
      }, false);
  },

  computeFrame: function() {
    this.ctx1.drawImage(this.video, 0, 0, this.width, this.height);
    let frame = this.ctx1.getImageData(0, 0, this.width, this.height);
		let l = frame.data.length / 4;

    for (let i = 0; i < l; i++) {
      let r = frame.data[i * 4 + 0] & 0xF8;
      let g = frame.data[i * 4 + 1] & 0xFC;
      let b = frame.data[i * 4 + 2] & 0xF8;
      frame.data[i * 4 + 0] = r;
      frame.data[i * 4 + 1] = g;
      frame.data[i * 4 + 2] = b;
      frame.data[i * 4 + 3] = 0;
      this.bytearray[i * 2] = (r & 0xF8) | (g >> 5);
      this.bytearray[i * 2 + 1] = ((g & 0x1C) << 3) | (b >> 3);
    }
    this.ws.send(this.bytearray.buffer);
    this.ctx2.putImageData(frame, 0, 0);
    return;
  }
};
