
find ./chrome ./components -name "$2" | while read filename
do
  width=$(identify -format '%w' "$filename");
  height=$(identify -format '%w' "$filename");
  psize=0.16;
 #swidth=$(echo "scale=0;($width - $psize*$width)/1" | bc);
  sheight=$(echo "scale=0;($height - $psize*$height)/1" | bc);
  echo " $filename ";
  if [ -z "$3" ]
  then
        echo "\$3 is empty";
  else
        echo "Icon color parameter is NOT empty : $3";
        sed -e "s/svg xmlns/svg fill='$3' xmlns/g" "$1" > /tmp/tmp.svg
        mv /tmp/tmp.svg "$1"
  fi
  rsvg-convert "$1" -h "$sheight" -o /tmp/tmp.png;
  convert /tmp/tmp.png -background none -gravity center -extent "$width"'x'"$height" "$filename";
done
