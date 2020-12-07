/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package PowerMeasurements

import java.io.EOFException
import java.util.Comparator

import com.google.common.primitives.UnsignedBytes
import org.apache.hadoop.fs.{FSDataInputStream, FileStatus, FileSystem, Path}
import org.apache.hadoop.mapreduce.{InputSplit, JobContext, RecordReader, TaskAttemptContext}
import org.apache.hadoop.mapreduce.lib.input.{FileInputFormat, FileSplit}

import scala.collection.JavaConversions._

object TeraLongInputFormat {
   val KEY_LEN = 10
   val VALUE_LEN = 90
   val VALUE_SCALE_FACTOR = 0
   val SCALED_VALUE_LEN : Int = VALUE_SCALE_FACTOR * VALUE_LEN
   val INPUT_RECORD_LEN : Int = KEY_LEN + VALUE_LEN
   val OUTPUT_RECORD_LEN : Int = KEY_LEN + SCALED_VALUE_LEN
   var lastContext : JobContext = _
   var lastResult : java.util.List[InputSplit] = _
   implicit val caseInsensitiveOrdering : Comparator[Array[Byte]] =
     UnsignedBytes.lexicographicalComparator
}

class TeraLongInputFormat extends FileInputFormat[Array[Byte], Array[Byte]] {

  override def createRecordReader(split: InputSplit, context: TaskAttemptContext)
  : RecordReader[Array[Byte], Array[Byte]] = new TeraRecordReader()

  // Sort the file pieces since order matters.
  override def listStatus(job: JobContext): java.util.List[FileStatus] = {
    val listing = super.listStatus(job)
    val sortedListing= listing.sortWith{ (lhs, rhs) => { 
      lhs.getPath.compareTo(rhs.getPath) < 0
    } }
    sortedListing.toList
  }

  class TeraRecordReader extends RecordReader[Array[Byte], Array[Byte]] {
    private var in : FSDataInputStream = _
    private var offset: Long = 0
    private var length: Long = 0
    private val buffer: Array[Byte] = new Array[Byte](TeraLongInputFormat.INPUT_RECORD_LEN)
    private var key: Array[Byte] = _
    private var value: Array[Byte] = _

    override def nextKeyValue() : Boolean = {
      if (offset >= length) {
        return false
      }
      var read : Int = 0
      while (read < TeraLongInputFormat.INPUT_RECORD_LEN) {
        var newRead : Int = in.read(buffer, read, TeraLongInputFormat.INPUT_RECORD_LEN - read)
        if (newRead == -1) {
          if (read == 0) false
          else throw new EOFException("read past eof")
        }
        read += newRead
      }
      if (key == null) {
        key = new Array[Byte](TeraLongInputFormat.KEY_LEN)
      }
      if (value == null) {
        value = new Array[Byte](TeraLongInputFormat.SCALED_VALUE_LEN)
      }

      // Copy Key
      buffer.copyToArray(key, 0, TeraLongInputFormat.KEY_LEN)

      // Copy Value from Input Record and scale its size to desired number of times
      var iter : Int = 0
      while (iter < TeraLongInputFormat.VALUE_SCALE_FACTOR){
        // TODO: Possible CPU load in favor of memory? Should I take right into a temp variable first?
        buffer.takeRight(TeraLongInputFormat.VALUE_LEN).copyToArray(value, iter * TeraLongInputFormat.VALUE_LEN, TeraLongInputFormat.VALUE_LEN)
        iter += 1
      }

      offset += TeraLongInputFormat.INPUT_RECORD_LEN
      true
    }

    override def initialize(split : InputSplit, context : TaskAttemptContext) : Unit = {
      val fileSplit = split.asInstanceOf[FileSplit]
      val p : Path = fileSplit.getPath
      val fs : FileSystem = p.getFileSystem(context.getConfiguration)
      in = fs.open(p)
      val start : Long = fileSplit.getStart
      // find the offset to start at a record boundary
      val reclen = TeraLongInputFormat.INPUT_RECORD_LEN
      offset = (reclen - (start % reclen)) % reclen
      in.seek(start + offset)
      length = fileSplit.getLength
    }

    override def close() : Unit = in.close()
    override def getCurrentKey : Array[Byte] = key
    override def getCurrentValue : Array[Byte] = value
    override def getProgress : Float = offset / length
  }

}
