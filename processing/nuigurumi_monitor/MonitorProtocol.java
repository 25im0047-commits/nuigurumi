/** Arduinoのログを一行単位で検証。説明文や途中の行は計測値にしない。 */
public final class MonitorProtocol {
  public static final class Sample {
    public final int raw0, raw1, level0, level1, state, ready;
    public final int calibrated, capture, audio, muted;
    public final boolean full;
    Sample(int[] v, boolean full) {
      raw0=v[0]; raw1=v[1]; level0=v[2]; level1=v[3]; state=v[4]; ready=v[5];
      calibrated=v[6]; capture=v[7]; audio=v[8]; muted=v[9]; this.full=full;
    }
  }
  public static Sample parse(String line) {
    if (line == null || line.length() > 240) return null;
    String[] names={"raw0","raw1","level0","level1","state","ready","cal","capture","audio","muted"};
    int[] v={-1,-1,-1,-1,-1,-1,-1,-3,-1,-1};
    boolean[] seen=new boolean[names.length];
    String trimmed=line.trim();
    if (trimmed.isEmpty()) return null;
    for (String field : trimmed.split("\\s+")) {
      String[] pair=field.split(":", -1);
      if (pair.length != 2) return null;
      int index=-1;
      for (int i=0;i<names.length;i++) if(names[i].equals(pair[0])) index=i;
      if(index < 0) continue; // 将来の追加フィールドは許容
      if(seen[index] || !pair[1].matches("-?[0-9]{1,4}")) return null;
      seen[index]=true;
      try { v[index]=Integer.parseInt(pair[1]); } catch(NumberFormatException e) { return null; }
    }
    if(!seen[0] || !seen[1] || v[0]<0 || v[0]>1023 || v[1]<0 || v[1]>1023) return null;
    boolean full=seen[2] || seen[3] || seen[4] || seen[5];
    if(full && (!seen[2] || !seen[3] || !seen[4] || !seen[5] ||
       v[2]<0 || v[2]>1000 || v[3]<0 || v[3]>1000 || v[4]<0 || v[4]>3 || v[5]<0 || v[5]>1)) return null;
    for(int i : new int[]{6,8,9}) if(seen[i] && (v[i]<0 || v[i]>1)) return null;
    if(seen[7] && (v[7]<-2 || v[7]>1)) return null;
    // 新しい状態通知はまとめて受け取り、食い違う状態では操作させない。
    boolean status=seen[6] || seen[7] || seen[8] || seen[9];
    if(status && (!full || !seen[6] || !seen[7] || !seen[8] || !seen[9] ||
       (v[5]==1 && v[6]!=1) || (v[7]!=-2 && (v[5]!=0 || v[6]!=0)))) return null;
    return new Sample(v, full);
  }
}
