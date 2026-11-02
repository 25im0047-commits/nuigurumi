import java.nio.file.*;
import java.util.*;
public class ProtocolTest {
  static void check(boolean value) { if(!value) throw new AssertionError(); }
  public static void main(String[] args) throws Exception {
    String full="raw0:1023\traw1:0\tlevel0:1000\tlevel1:12\tstate:3\tready:1\tcal:1\tcapture:-2\taudio:0\tmuted:1";
    MonitorProtocol.Sample sample=MonitorProtocol.parse(full);
    check(sample!=null && sample.raw0==1023 && sample.level0==1000 && sample.calibrated==1 && sample.capture==-2);
    check(MonitorProtocol.parse("raw0:31 raw1:30")!=null);
    check(MonitorProtocol.parse("raw0:31 raw1:30 level0:0 level1:0 state:0 ready:0").calibrated==-1);
    for(String bad : new String[]{"", "DONE: rest captured.", "32,35", "raw0:1", "raw0:-1 raw1:2", "raw0:1024 raw1:0", "raw0:NaN raw1:0", "raw0:2 raw1:3 level0:8", "raw0:2 raw0:3 raw1:4", "raw0:2 raw1:4 extra", full.replace("state:3","state:4"), full.replace("ready:1","ready:2"), full.replace("level0:1000","level0:1001"), full.replace("cal:1","cal:0"), full.replace("capture:-2","capture:0"), full.replace("audio:0","audio:-1"), full.replace("capture:-2","capture:-3"), full.replace("\tmuted:1","")}) check(MonitorProtocol.parse(bad)==null);
    check(MonitorProtocol.parse(null)==null);
    check(MonitorProtocol.parse(full+" future:2")!=null);
    check(MonitorProtocol.parse(full+"x".repeat(241))==null);
    Random rng=new Random(42);
    for(int i=0;i<20000;i++) {
      char[] chars=new char[rng.nextInt(260)];
      for(int j=0;j<chars.length;j++) chars[j]=(char)rng.nextInt(128);
      MonitorProtocol.parse(new String(chars)); // 壊れた受信行で例外を起こさない
    }
    int count=0;
    for(String line:Files.readAllLines(Path.of(args[0]))) {
      MonitorProtocol.Sample s=MonitorProtocol.parse(line);
      check(s!=null); count++;
      check(line.length()+2 < 192); // 200ms / 9600 baud / start+stop bits
    }
    check(count>=5);
    System.out.println("PASS: firmware log round-trip ("+count+" frames), old logs, bounds, partial rows, duplicate fields, corrupt states, 20000 malformed lines");
  }
}
