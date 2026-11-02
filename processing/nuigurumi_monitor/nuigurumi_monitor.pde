import processing.serial.*;
import java.util.concurrent.ConcurrentLinkedQueue;

// NUIGURUMI — 2つのフォトリフレクタを、同じ目盛りで見る。
final int BAUD = 9600;
final int HISTORY = 300;
final int STALE_MS = 1800;
Serial device;
String[] ports = new String[0];
int selected = 0;
ConcurrentLinkedQueue<String> inbox = new ConcurrentLinkedQueue<String>();
MonitorProtocol.Sample sample;
int receivedAt = -1;
int connectedAt;
boolean requestedStream = false;
boolean demo = false;
float demoLevel = 0.38;
int demoState = 0;
int demoCandidate = 0;
int demoCandidateAt = 0;
int lastDemo = 0;
int historyHead = 0;
int historyCount = 0;
MonitorProtocol.Sample[] history = new MonitorProtocol.Sample[HISTORY];
String notice = "USBでつないで、接続先を選んでください。";
String boardNotice = "校正は、ぬいぐるみから手を離して始めます。";
int commandUntil = 0;
int lastPacketAt = -1;
int ink, subtle, border, mint, coral, purple;

void setup() {
  size(1120, 820);
  pixelDensity(displayDensity());
  smooth(4);
  frameRate(30);
  surface.setTitle("NUIGURUMI / やさしく、ふれる");
  textFont(createFont("SansSerif", 16, true));
  ink=color(40, 54, 54); subtle=color(109, 124, 121); border=color(223, 231, 226);
  mint=color(45, 147, 119); coral=color(210, 111, 88); purple=color(126, 125, 194);
  refreshPorts();
}

boolean fresh() { return sample != null && receivedAt >= 0 && millis()-receivedAt < STALE_MS; }
boolean actionable() {
  return device != null && !demo && fresh() && sample.full && sample.capture == -2 && millis() >= commandUntil;
}

void draw() {
  readPackets();
  if (device != null && !requestedStream && millis()-connectedAt > 4000) {
    requestedStream = true;
    sendCommand('v', "状態通知をリクエストしました。");
  }
  if (demo && millis()-lastDemo >= 200) updateDemo();
  if (!demo && lastPacketAt >= 0 && millis()-lastPacketAt >= STALE_MS) {
    addHistory(null); lastPacketAt=-1;
  }
  background(245, 247, 242);
  fill(ink); textSize(13); text("TOUCH & FEEL", 32, 33);
  textSize(30); text("やさしく、ふれる。", 30, 73);
  fill(subtle); textSize(13); text("NUIGURUMI   /   ぬいぐるみの押し加減モニター", 32, 100);
  pill(demo ? "DEMO · サンプル表示" : fresh() ? "受信中" : device != null ? (sample==null ? "受信待ち" : "受信停止") : "未接続", 882, 37, 205, demo ? purple : fresh() ? mint : subtle);

  panel(28, 122, 316, 654);
  drawConnection();
  drawCalibration();
  drawFeedback();
  drawGraph();
  fill(subtle); textSize(11);
  text("押し加減は校正した変形の割合です。力（N）や距離（cm）ではありません。", 32, 802);
}

void drawConnection() {
  fill(ink); textSize(18); text("接続", 48, 155);
  fill(subtle); textSize(12); text(demo ? "実機への送信は停止しています" : "USBシリアル / 9600 baud", 48, 177);
  button("接続先を切り替え  ›", 48, 194, 276, 34, device == null && !demo, false);
  fill(ink); textSize(11);
  String path=ports.length == 0 ? "接続先が見つかりません" : ports[selected];
  text(shortPath(path), 48, 250);
  button(device == null ? "接続する" : "切断する", 48, 266, 170, 38, !demo && (device != null || ports.length>0), true);
  button("再検索", 226, 266, 98, 38, device == null && !demo, false);
  button(demo ? "デモを終了" : "デモで試す", 48, 316, 276, 36, device == null, false);
  fill(subtle); textSize(12); text(notice, 48, 364, 276, 64);
}

void drawCalibration() {
  fill(border); rect(48, 427, 276, 1);
  fill(ink); textSize(18); text("校正", 48, 459);
  fill(subtle); textSize(12); text("① 手を離す → ②③ 大人が基準を決める", 48, 482);
  button("① 無負荷を測る  ·  約1秒", 48, 498, 276, 37, actionable(), false);
  button("② A0の押し基準を測る", 48, 545, 276, 37, actionable(), false);
  button("③ A1の押し基準を測る", 48, 592, 276, 37, actionable(), false);
  button("④ 校正を保存", 48, 639, 170, 37, actionable() && sample.calibrated == 1, true);
  button(sample != null && sample.muted == 1 ? "音を戻す" : "消音", 226, 639, 98, 37, actionable(), false);
  fill(subtle); textSize(12); text(boardNotice, 48, 690, 276, 67);
}

void drawFeedback() {
  panel(364, 122, 728, 282);
  boolean valid=fresh() && sample.full && sample.ready==1;
  int state=valid ? sample.state : 0;
  int accent=demo ? purple : !valid ? subtle : state==3 ? coral : state==2 ? color(178,139,58) : mint;
  String title=!fresh() ? (device==null && !demo ? "ぬいぐるみを、つないでみよう" : "データを待っています") :
    !sample.full ? "生の値を受信中" : sample.capture != -2 && sample.capture != -3 ? "そのまま、約1秒キープ" :
    sample.calibrated==0 ? "まずは、押し基準を決めよう" : sample.ready==0 ? "一度、手を離してね" :
    state==0 ? "そっと、ふれてみよう" : state==1 ? "やさしくできてるね" : state==2 ? "少し、ゆるめてみよう" : "ゆっくり、手をゆるめよう";
  drawBear(455, 233, accent, valid ? max(sample.level0,sample.level1)/1000.0 : 0);
  fill(subtle); textSize(12); text(demo ? "DEMO / 実機データではありません" : "いまの押し加減", 556, 156);
  fill(ink); textSize(23); text(title, 556, 192);
  int value=sample == null ? 0 : max(sample.level0,sample.level1);
  textSize(48); text(valid ? nf(value/10.0,0,1) : "—", 556, 254);
  textSize(16); fill(subtle); text("%", 697, 251);
  String hint=demo ? "下のバーをドラッグして反応を試せます" : !fresh() ? "値が止まった時は、接続先とUSBを確認" :
    !sample.full ? "状態通知ボタンで通常表示に戻せます" : sample.calibrated==0 ? "左の①〜③で準備できます" : "大人が決めた変形を100%として表示";
  textSize(12); text(hint, 556, 282);
  float meter=valid ? constrain(value/1000.0,0,1) : 0;
  noStroke(); fill(233,238,232); rect(556,300,506,12,6); fill(accent); rect(556,300,506*meter,12,6);
  if(demo) { fill(purple); circle(556+506*demoLevel,306,20); }
  fill(subtle); textSize(10); text("0",556,330); text("100%",1028,330);
  fill(border); rect(388,347,680,1);
  fill(subtle); textSize(12); text("A0  "+rawText(0)+"     A1  "+rawText(1), 389,378);
  button("状態通知", 925,358,143,30,device != null && !demo && millis() >= commandUntil,false);
}

void drawGraph() {
  panel(364,422,728,354);
  fill(ink); textSize(18); text("ふれ方の記録", 388,455);
  fill(mint); circle(807,449,7); fill(subtle); textSize(12); text("A0",819,454);
  fill(purple); circle(862,449,7); fill(subtle); text("A1",874,454);
  boolean levels=fresh() && sample.full && sample.ready==1;
  float ceiling=levels ? 1000 : rawCeiling();
  float floor=levels ? 0 : rawFloor();
  float gx=414, gy=484, gw=650, gh=202;
  if(levels) {
    noStroke(); fill(249,239,231); rect(gx,gy,gw,gh*0.15);
    fill(249,245,227); rect(gx,gy+gh*0.15,gw,gh*0.20);
  }
  for(int i=0;i<=4;i++) {
    float y=gy+gh*i/4.0;
    stroke(border); strokeWeight(1); line(gx,y,gx+gw,y);
    fill(subtle); textAlign(RIGHT); textSize(10);
    text(levels ? str(100-i*25)+"%" : str(round(lerp(ceiling,floor,i/4.0))), gx-8,y+4);
  }
  textAlign(LEFT);
  for(int channel=0;channel<2;channel++) {
    stroke(channel==0 ? mint : purple); strokeWeight(2.3);
    for(int i=1;i<historyCount;i++) {
      MonitorProtocol.Sample a=history[(historyHead-historyCount+i-1+HISTORY)%HISTORY];
      MonitorProtocol.Sample b=history[(historyHead-historyCount+i+HISTORY)%HISTORY];
      if(a==null || b==null || (levels && (!a.full || !b.full || a.ready!=1 || b.ready!=1))) continue;
      float av=levels ? (channel==0 ? a.level0 : a.level1) : (channel==0 ? a.raw0 : a.raw1);
      float bv=levels ? (channel==0 ? b.level0 : b.level1) : (channel==0 ? b.raw0 : b.raw1);
      float x0=gx+gw*(HISTORY-historyCount+i-1)/(HISTORY-1.0);
      float x1=gx+gw*(HISTORY-historyCount+i)/(HISTORY-1.0);
      line(x0,map(constrain(av,floor,ceiling),floor,ceiling,gy+gh,gy),x1,map(constrain(bv,floor,ceiling),floor,ceiling,gy+gh,gy));
    }
  }
  fill(subtle); textSize(11); text("← 過去300サンプル", gx,710); text("いま",1035,710);
  String legend=levels ? "押し加減 / 65%・85%の色帯は標準設定の目安" : "生のADC値 / 見やすい範囲に自動調整";
  text(legend,388,747);
  if(!fresh()) { fill(255,245); noStroke(); rect(gx,gy,gw,gh); fill(subtle); textAlign(CENTER); textSize(15); text("受信すると、ここにグラフが流れます",gx+gw/2,gy+gh/2); textAlign(LEFT); }
}

String rawText(int channel) {
  if(!fresh()) return "—";
  return str(channel==0 ? sample.raw0 : sample.raw1)+" ADC";
}
float rawFloor() {
  int value=1023; boolean found=false;
  for(MonitorProtocol.Sample s:history) if(s!=null) { value=min(value,min(s.raw0,s.raw1)); found=true; }
  return found ? max(0,value-8) : 0;
}
float rawCeiling() {
  int value=0; boolean found=false;
  for(MonitorProtocol.Sample s:history) if(s!=null) { value=max(value,max(s.raw0,s.raw1)); found=true; }
  return found ? max(rawFloor()+16,min(1023,value+8)) : 1023;
}
void panel(float x,float y,float w,float h) { noStroke(); fill(255); rect(x,y,w,h,18); }
void pill(String label,float x,float y,float w,int c) { noStroke(); fill(c,18); rect(x,y,w,32,16); fill(c); textSize(12); textAlign(CENTER); text(label,x+w/2,y+21); textAlign(LEFT); }
void button(String label,float x,float y,float w,float h,boolean enabled,boolean primary) {
  boolean hover=enabled && hit(x,y,w,h);
  stroke(enabled ? (primary ? mint : border) : color(237)); strokeWeight(1);
  fill(!enabled ? color(245,246,243) : primary ? (hover ? color(34,126,100) : mint) : hover ? color(238,245,239) : color(255));
  rect(x,y,w,h,9); noStroke(); fill(!enabled ? color(168,178,170) : primary ? color(255) : ink);
  textSize(12); textAlign(CENTER,CENTER); text(label,x+w/2,y+h/2-1); textAlign(LEFT,BASELINE);
}
boolean hit(float x,float y,float w,float h) { return mouseX>=x && mouseX<=x+w && mouseY>=y && mouseY<=y+h; }
String shortPath(String value) { return value.length()>38 ? "…"+value.substring(value.length()-37) : value; }

void drawBear(float x,float y,int accent,float amount) {
  pushMatrix(); translate(x,y); scale(1+amount*0.07,1-amount*0.06);
  noStroke(); fill(226,233,219); ellipse(-43,-48,43,46); ellipse(43,-48,43,46);
  fill(242,228,208); ellipse(-43,-48,24,27); ellipse(43,-48,24,27);
  fill(236,227,207); ellipse(0,4,132,127);
  fill(ink); ellipse(-23,-3,6,7); ellipse(23,-3,6,7);
  fill(250,244,230); ellipse(0,24,49,35); fill(ink); ellipse(0,17,11,8);
  noFill(); stroke(ink); strokeWeight(2); arc(-7,21,14,13,0,PI); arc(7,21,14,13,0,PI);
  noStroke(); fill(accent,65); ellipse(-38,18,20,11); ellipse(38,18,20,11);
  popMatrix();
}

void refreshPorts() {
  String previous=ports.length>0 ? ports[selected] : null;
  try {
    ports=Serial.list(); selected=0;
    boolean restored=false;
    for(int i=0;i<ports.length;i++) if(ports[i].equals(previous)) { selected=i; restored=true; }
    if(!restored) {
      int usb=-1, count=0;
      for(int i=0;i<ports.length;i++) {
        String name=ports[i].toLowerCase();
        if(!name.startsWith("/dev/tty.") && (name.contains("usbmodem") || name.contains("usbserial") || name.contains("ttyacm") || name.contains("ttyusb"))) { usb=i; count++; }
      }
      if(count==1) selected=usb; // USB候補が1個だけなら選択。接続はボタン操作で行う。
    }
  }
  catch(RuntimeException e) { ports=new String[0]; notice="接続先を取得できませんでした。"; }
}
void clearData() {
  inbox.clear(); sample=null; receivedAt=-1; lastPacketAt=-1;
  historyHead=historyCount=0; history=new MonitorProtocol.Sample[HISTORY]; commandUntil=0;
}
void connectDevice() {
  if(ports.length==0) return;
  clearData(); boardNotice="起動中はぬいぐるみから手を離してください。";
  try {
    device=new Serial(this,ports[selected],BAUD);
    device.clear(); device.bufferUntil('\n');
    connectedAt=millis(); requestedStream=false;
    notice="起動待ちです。手を離したまま約4秒待ちます。";
  } catch(RuntimeException e) {
    disconnectDevice(); notice="接続できません。Arduinoのモニタを閉じて再試行。";
  }
}
void disconnectDevice() {
  Serial old=device; device=null;
  if(old!=null) try { old.stop(); } catch(RuntimeException e) { }
  clearData(); notice="切断しました。"; boardNotice="校正は、接続してから始められます。";
}
void serialEvent(Serial source) {
  if(source!=device) return;
  String line=source.readStringUntil('\n');
  if(line!=null && line.length()<=240 && inbox.size()<64) inbox.offer(line);
}
void readPackets() {
  String line;
  int budget=0;
  while(budget++<64 && (line=inbox.poll())!=null) {
    MonitorProtocol.Sample incoming=MonitorProtocol.parse(line);
    if(incoming!=null) {
      sample=incoming; receivedAt=millis(); lastPacketAt=millis(); addHistory(incoming);
      notice="受信中。切断後は接続ボタンでつなぎ直せます。";
    } else {
      line=line.trim();
      if(line.startsWith("DONE:")) boardNotice="計測できました。手を離して次の手順へ。";
      else if(line.startsWith("FAILED:")) boardNotice="校正できませんでした。固定と押し幅を確認し、再試行。";
      else if(line.equals("Saved and verified.")) boardNotice="校正値を保存し、読み戻しも確認しました。";
      else if(line.startsWith("Save refused:") || line.equals("Save verification failed.")) boardNotice="保存できませんでした。①〜③と接続を確認。";
      else if(line.startsWith("First release")) boardNotice="最初に①で無負荷を測ってください。";
      else if(line.startsWith("Unknown command.")) boardNotice="Arduino側も最新版に書き換えてください。";
      else if(line.startsWith("CAPTURING:")) boardNotice="そのまま約1秒キープ。完了後は手を離します。";
      else if(line.startsWith("Audio error")) boardNotice="音声装置からエラー。配線とSDカードを確認。";
    }
  }
}
void addHistory(MonitorProtocol.Sample value) { history[historyHead]=value; historyHead=(historyHead+1)%HISTORY; historyCount=min(HISTORY,historyCount+1); }
void sendCommand(char value,String message) {
  if(device==null || demo) return;
  try {
    device.write(value); boardNotice=message;
    // 校正開始の応答が届く前の連打も抑える。
    commandUntil=millis()+((value=='b' || value=='1' || value=='2') ? 1500 : 350);
  } catch(RuntimeException e) { disconnectDevice(); notice="通信が途切れました。USBを確認して再接続。"; }
}
void toggleDemo() {
  if(device!=null) return;
  demo=!demo; clearData(); demoState=demoCandidate=0; demoCandidateAt=millis();
  notice=demo ? "バーを動かすと、押し加減の変化を試せます。" : "USBでつないで、接続先を選んでください。";
  boardNotice=demo ? "デモ中の校正・保存・音声操作はできません。" : "校正は、ぬいぐるみから手を離して始めます。";
  if(demo) updateDemo();
}
void updateDemo() {
  lastDemo=millis(); int value=round(demoLevel*1000);
  int wanted=value>=850 ? 3 : value>=650 ? 2 : value>=150 ? 1 : 0;
  int[] boundaries={0,150,650,850};
  if(wanted<demoState && value>=boundaries[demoState]-40) wanted=demoState;
  if(wanted!=demoCandidate) { demoCandidate=wanted; demoCandidateAt=millis(); }
  if(millis()-demoCandidateAt>=120) demoState=demoCandidate;
  sample=MonitorProtocol.parse("raw0:"+round(52-demoLevel*25)+" raw1:"+round(50-demoLevel*18)+" level0:"+value+" level1:"+round(value*0.78)+" state:"+demoState+" ready:1 cal:1 capture:-2 audio:0 muted:1");
  receivedAt=millis(); addHistory(sample);
}
void mousePressed() {
  if(hit(48,194,276,34) && device==null && !demo && ports.length>0) selected=(selected+1)%ports.length;
  else if(hit(48,266,170,38) && !demo) { if(device==null) connectDevice(); else disconnectDevice(); }
  else if(hit(226,266,98,38) && device==null && !demo) refreshPorts();
  else if(hit(48,316,276,36) && device==null) toggleDemo();
  else if(hit(925,358,143,30) && device!=null && !demo && millis()>=commandUntil) sendCommand('v',"状態通知をリクエストしました。");
  else if(actionable()) {
    if(hit(48,498,276,37)) sendCommand('b',"手を離して、約1秒待ってください。");
    else if(hit(48,545,276,37)) sendCommand('1',"A0を大人が決めた基準で押し、約1秒キープ。");
    else if(hit(48,592,276,37)) sendCommand('2',"A1を大人が決めた基準で押し、約1秒キープ。");
    else if(hit(48,639,170,37) && sample.calibrated==1) sendCommand('s',"校正値を保存しています。");
    else if(hit(226,639,98,37)) sendCommand('m',"音声の設定を切り替えました。");
  }
  moveDemoSlider();
}
void mouseDragged() { moveDemoSlider(); }
void moveDemoSlider() { if(demo && hit(545,290,530,38)) demoLevel=constrain((mouseX-556)/506.0,0,1); }
void keyPressed() {
  if(key=='d' || key=='D') toggleDemo();
  if(key=='r' || key=='R') if(device==null && !demo) refreshPorts();
}
void exit() { if(device!=null) { device.stop(); device=null; } super.exit(); }
